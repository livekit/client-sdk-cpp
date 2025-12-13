/*
 * Copyright 2025 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "livekit/audio_stream.h"

#include <utility>

#include "audio_frame.pb.h"
#include "ffi.pb.h"
#include "livekit/ffi_client.h"
#include "livekit/track.h"

namespace livekit {

using proto::FfiEvent;
using proto::FfiRequest;

// ------------------------
// Factory helpers
// ------------------------

std::shared_ptr<AudioStream>
AudioStream::fromTrack(const std::shared_ptr<Track> &track,
                       const Options &options) {
  auto stream = std::shared_ptr<AudioStream>(new AudioStream());
  stream->initFromTrack(track, options);
  return stream;
}

std::shared_ptr<AudioStream>
AudioStream::fromParticipant(Participant &participant, TrackSource track_source,
                             const Options &options) {
  auto stream = std::shared_ptr<AudioStream>(new AudioStream());
  stream->initFromParticipant(participant, track_source, options);
  return stream;
}

// ------------------------
// Destructor / move
// ------------------------

AudioStream::~AudioStream() { close(); }

AudioStream::AudioStream(AudioStream &&other) noexcept {
  std::lock_guard<std::mutex> lock(other.mutex_);
  queue_ = std::move(other.queue_);
  capacity_ = other.capacity_;
  eof_ = other.eof_;
  closed_ = other.closed_;
  options_ = other.options_;
  stream_handle_ = std::move(other.stream_handle_);
  listener_id_ = other.listener_id_;

  other.listener_id_ = 0;
  other.closed_ = true;
}

AudioStream &AudioStream::operator=(AudioStream &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  close();

  {
    std::lock_guard<std::mutex> lock_this(mutex_);
    std::lock_guard<std::mutex> lock_other(other.mutex_);

    queue_ = std::move(other.queue_);
    capacity_ = other.capacity_;
    eof_ = other.eof_;
    closed_ = other.closed_;
    options_ = other.options_;
    stream_handle_ = std::move(other.stream_handle_);
    listener_id_ = other.listener_id_;

    other.listener_id_ = 0;
    other.closed_ = true;
  }

  return *this;
}

bool AudioStream::read(AudioFrameEvent &out_event) {
  std::unique_lock<std::mutex> lock(mutex_);

  cv_.wait(lock, [this] { return !queue_.empty() || eof_ || closed_; });

  if (closed_ || (queue_.empty() && eof_)) {
    return false; // EOS / closed
  }

  out_event = std::move(queue_.front());
  queue_.pop_front();
  return true;
}

void AudioStream::close() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return;
    }
    closed_ = true;
  }

  // Dispose FFI handle
  if (stream_handle_.get() != 0) {
    stream_handle_.reset();
  }

  // Remove listener
  if (listener_id_ != 0) {
    FfiClient::instance().RemoveListener(listener_id_);
    listener_id_ = 0;
  }

  // Wake any waiting readers
  cv_.notify_all();
}

// Internal functions

void AudioStream::initFromTrack(const std::shared_ptr<Track> &track,
                                const Options &options) {
  capacity_ = options.capacity;
  options_ = options;

  // 1) Subscribe to FFI events
  listener_id_ = FfiClient::instance().AddListener(
      [this](const FfiEvent &e) { this->onFfiEvent(e); });

  // 2) Send FfiRequest to create a new audio stream bound to this track
  FfiRequest req;
  auto *new_audio_stream = req.mutable_new_audio_stream();
  new_audio_stream->set_track_handle(
      static_cast<uint64_t>(track->ffi_handle_id()));
  // TODO, sample_rate and num_channels are not useful in AudioStream, remove it
  // from FFI.
  //  new_audio_stream->set_sample_rate(options_.sample_rate);
  //  new_audio_stream->set_num_channels(options.num_channels);
  new_audio_stream->set_type(proto::AudioStreamType::AUDIO_STREAM_NATIVE);

  if (!options_.noise_cancellation_module.empty()) {
    new_audio_stream->set_audio_filter_module_id(
        options_.noise_cancellation_module);
    // Always set options JSON even if empty — backend will treat empty string
    // as “no options”
    new_audio_stream->set_audio_filter_options(
        options_.noise_cancellation_options_json);
  }

  auto resp = FfiClient::instance().sendRequest(req);
  const auto &stream = resp.new_audio_stream().stream();
  stream_handle_ = FfiHandle(static_cast<uintptr_t>(stream.handle().id()));
}

void AudioStream::initFromParticipant(Participant &participant,
                                      TrackSource track_source,
                                      const Options &options) {
  capacity_ = options.capacity;
  options_ = options;

  // 1) Subscribe to FFI events
  listener_id_ = FfiClient::instance().AddListener(
      [this](const FfiEvent &e) { this->onFfiEvent(e); });

  // 2) Send FfiRequest to create audio stream from participant + track source
  FfiRequest req;
  auto *as = req.mutable_audio_stream_from_participant();
  as->set_participant_handle(participant.ffiHandleId());
  // TODO, sample_rate and num_channels are not useful in AudioStream, remove it
  // from FFI.
  // as->set_sample_rate(options_.sample_rate);
  // as->set_num_channels(options_.num_channels);
  as->set_type(proto::AudioStreamType::AUDIO_STREAM_NATIVE);
  as->set_track_source(static_cast<proto::TrackSource>(track_source));

  if (!options_.noise_cancellation_module.empty()) {
    as->set_audio_filter_module_id(options_.noise_cancellation_module);
    // Always set options JSON even if empty — backend will treat empty string
    // as “no options”
    as->set_audio_filter_options(options_.noise_cancellation_options_json);
  }

  auto resp = FfiClient::instance().sendRequest(req);
  const auto &stream = resp.audio_stream_from_participant().stream();
  stream_handle_ = FfiHandle(static_cast<uintptr_t>(stream.handle().id()));
}

void AudioStream::onFfiEvent(const FfiEvent &event) {
  if (event.message_case() != FfiEvent::kAudioStreamEvent) {
    return;
  }
  const auto &ase = event.audio_stream_event();
  // Check if this event is for our stream handle.
  if (ase.stream_handle() != static_cast<std::uint64_t>(stream_handle_.get())) {
    return;
  }
  if (ase.has_frame_received()) {
    const auto &fr = ase.frame_received();
    AudioFrame frame = AudioFrame::fromOwnedInfo(fr.frame());
    AudioFrameEvent ev{std::move(frame)};
    pushFrame(std::move(ev));
  } else if (ase.has_eos()) {
    pushEos();
  }
}

void AudioStream::pushFrame(AudioFrameEvent &&ev) {
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (closed_ || eof_) {
      return;
    }

    if (capacity_ > 0 && queue_.size() >= capacity_) {
      // Ring behavior: drop oldest frame when full.
      queue_.pop_front();
    }

    queue_.push_back(std::move(ev));
  }
  cv_.notify_one();
}

void AudioStream::pushEos() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (eof_) {
      return;
    }
    eof_ = true;
  }
  cv_.notify_all();
}

} // namespace livekit
