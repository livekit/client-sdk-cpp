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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "livekit/audio_stream.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

#include "audio_frame.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/ffi_handle.h"
#include "livekit/participant.h"
#include "livekit/track.h"

namespace livekit {

using proto::FfiEvent;
using proto::FfiRequest;

struct AudioStream::Impl {
  ~Impl() { close(); }

  bool read(AudioFrameEvent &out_event) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || eof_ || closed_; });

    if (closed_ || (queue_.empty() && eof_)) {
      return false;
    }

    out_event = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }

  void close() {
    FfiHandle stream_handle;
    std::int32_t listener_id = 0;
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      if (closed_) {
        return;
      }
      closed_ = true;
      stream_handle = std::move(stream_handle_);
      listener_id = listener_id_;
      listener_id_ = 0;
    }

    if (stream_handle.get() != 0) {
      stream_handle.reset();
    }
    if (listener_id != 0) {
      FfiClient::instance().RemoveListener(listener_id);
    }

    cv_.notify_all();
  }

  void initFromTrack(const std::shared_ptr<Track> &track,
                     const Options &options) {
    capacity_ = options.capacity;
    options_ = options;

    listener_id_ = FfiClient::instance().AddListener(
        [this](const FfiEvent &e) { this->onFfiEvent(e); });

    FfiRequest req;
    auto *new_audio_stream = req.mutable_new_audio_stream();
    new_audio_stream->set_track_handle(
        static_cast<uint64_t>(track->ffi_handle_id()));
    new_audio_stream->set_type(proto::AudioStreamType::AUDIO_STREAM_NATIVE);

    if (!options_.noise_cancellation_module.empty()) {
      new_audio_stream->set_audio_filter_module_id(
          options_.noise_cancellation_module);
      new_audio_stream->set_audio_filter_options(
          options_.noise_cancellation_options_json);
    }

    auto resp = FfiClient::instance().sendRequest(req);
    const auto &stream = resp.new_audio_stream().stream();
    stream_handle_ = FfiHandle(static_cast<uintptr_t>(stream.handle().id()));
  }

  void initFromParticipant(Participant &participant, TrackSource track_source,
                           const Options &options) {
    capacity_ = options.capacity;
    options_ = options;

    listener_id_ = FfiClient::instance().AddListener(
        [this](const FfiEvent &e) { this->onFfiEvent(e); });

    FfiRequest req;
    auto *as = req.mutable_audio_stream_from_participant();
    as->set_participant_handle(participant.ffiHandleId());
    as->set_type(proto::AudioStreamType::AUDIO_STREAM_NATIVE);
    as->set_track_source(static_cast<proto::TrackSource>(track_source));

    if (!options_.noise_cancellation_module.empty()) {
      as->set_audio_filter_module_id(options_.noise_cancellation_module);
      as->set_audio_filter_options(options_.noise_cancellation_options_json);
    }

    auto resp = FfiClient::instance().sendRequest(req);
    const auto &stream = resp.audio_stream_from_participant().stream();
    stream_handle_ = FfiHandle(static_cast<uintptr_t>(stream.handle().id()));
  }

  void onFfiEvent(const FfiEvent &event) {
    if (event.message_case() != FfiEvent::kAudioStreamEvent) {
      return;
    }
    const auto &ase = event.audio_stream_event();
    if (ase.stream_handle() !=
        static_cast<std::uint64_t>(stream_handle_.get())) {
      return;
    }
    if (ase.has_frame_received()) {
      const auto &fr = ase.frame_received();
      AudioFrameEvent ev{AudioFrame::fromOwnedInfo(fr.frame())};
      pushFrame(std::move(ev));
    } else if (ase.has_eos()) {
      pushEos();
    }
  }

  void pushFrame(AudioFrameEvent &&ev) {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      if (closed_ || eof_) {
        return;
      }
      if (capacity_ > 0 && queue_.size() >= capacity_) {
        queue_.pop_front();
      }
      queue_.push_back(std::move(ev));
    }
    cv_.notify_one();
  }

  void pushEos() {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      if (eof_) {
        return;
      }
      eof_ = true;
    }
    cv_.notify_all();
  }

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<AudioFrameEvent> queue_;
  std::size_t capacity_{0};
  bool eof_{false};
  bool closed_{false};
  Options options_;
  FfiHandle stream_handle_;
  std::int32_t listener_id_{0};
};

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

AudioStream::AudioStream() : impl_(std::make_unique<Impl>()) {}

AudioStream::~AudioStream() = default;

AudioStream::AudioStream(AudioStream &&other) noexcept = default;

AudioStream &AudioStream::operator=(AudioStream &&other) noexcept = default;

bool AudioStream::read(AudioFrameEvent &out_event) {
  return impl_ ? impl_->read(out_event) : false;
}

void AudioStream::close() {
  if (impl_) {
    impl_->close();
  }
}

void AudioStream::initFromTrack(const std::shared_ptr<Track> &track,
                                const Options &options) {
  impl_->initFromTrack(track, options);
}

void AudioStream::initFromParticipant(Participant &participant,
                                      TrackSource track_source,
                                      const Options &options) {
  impl_->initFromParticipant(participant, track_source, options);
}

} // namespace livekit
