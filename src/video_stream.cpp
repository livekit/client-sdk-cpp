/*
 * Copyright 2026 LiveKit
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

#include "livekit/video_stream.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/ffi_handle.h"
#include "livekit/participant.h"
#include "livekit/track.h"
#include "lk_log.h"
#include "video_frame.pb.h"
#include "video_utils.h"

namespace livekit {

using proto::FfiEvent;
using proto::FfiRequest;
using proto::VideoStreamEvent;

struct VideoStream::Impl {
  ~Impl() { close(); }

  bool read(VideoFrameEvent &out) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || eof_ || closed_; });

    if (closed_ || (queue_.empty() && eof_)) {
      return false;
    }

    out = std::move(queue_.front());
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

    listener_id_ = FfiClient::instance().AddListener(
        [this](const proto::FfiEvent &e) { this->onFfiEvent(e); });

    FfiRequest req;
    auto *new_video_stream = req.mutable_new_video_stream();
    new_video_stream->set_track_handle(
        static_cast<uint64_t>(track->ffi_handle_id()));
    new_video_stream->set_type(proto::VideoStreamType::VIDEO_STREAM_NATIVE);
    new_video_stream->set_normalize_stride(true);
    new_video_stream->set_format(toProto(options.format));

    auto resp = FfiClient::instance().sendRequest(req);
    if (!resp.has_new_video_stream()) {
      LK_LOG_ERROR("VideoStream::initFromTrack: FFI response missing "
                   "new_video_stream()");
      throw std::runtime_error("new_video_stream FFI request failed");
    }
    const auto &stream = resp.new_video_stream().stream();
    stream_handle_ = FfiHandle(static_cast<uintptr_t>(stream.handle().id()));
  }

  void initFromParticipant(Participant &participant, TrackSource track_source,
                           const Options &options) {
    capacity_ = options.capacity;

    listener_id_ = FfiClient::instance().AddListener(
        [this](const FfiEvent &e) { this->onFfiEvent(e); });

    FfiRequest req;
    auto *vs = req.mutable_video_stream_from_participant();
    vs->set_participant_handle(participant.ffiHandleId());
    vs->set_type(proto::VideoStreamType::VIDEO_STREAM_NATIVE);
    vs->set_track_source(static_cast<proto::TrackSource>(track_source));
    vs->set_normalize_stride(true);
    vs->set_format(toProto(options.format));

    auto resp = FfiClient::instance().sendRequest(req);
    const auto &stream = resp.video_stream_from_participant().stream();
    stream_handle_ = FfiHandle(static_cast<uintptr_t>(stream.handle().id()));
  }

  void onFfiEvent(const proto::FfiEvent &event) {
    if (event.message_case() != FfiEvent::kVideoStreamEvent) {
      return;
    }
    const auto &vse = event.video_stream_event();
    if (vse.stream_handle() !=
        static_cast<std::uint64_t>(stream_handle_.get())) {
      return;
    }
    if (vse.has_frame_received()) {
      const auto &fr = vse.frame_received();
      VideoFrameEvent ev;
      ev.frame = VideoFrame::fromOwnedInfo(fr.buffer());
      ev.timestamp_us = fr.timestamp_us();
      ev.rotation = static_cast<VideoRotation>(fr.rotation());
      if (fr.has_metadata()) {
        ev.metadata = fromProto(fr.metadata());
      }
      pushFrame(std::move(ev));
    } else if (vse.has_eos()) {
      pushEos();
    }
  }

  void pushFrame(VideoFrameEvent &&ev) {
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
  std::deque<VideoFrameEvent> queue_;
  std::size_t capacity_{0};
  bool eof_{false};
  bool closed_{false};
  FfiHandle stream_handle_;
  std::int32_t listener_id_{0};
};

std::shared_ptr<VideoStream>
VideoStream::fromTrack(const std::shared_ptr<Track> &track,
                       const Options &options) {
  auto stream = std::shared_ptr<VideoStream>(new VideoStream());
  stream->initFromTrack(track, options);
  return stream;
}

std::shared_ptr<VideoStream>
VideoStream::fromParticipant(Participant &participant, TrackSource track_source,
                             const Options &options) {
  auto stream = std::shared_ptr<VideoStream>(new VideoStream());
  stream->initFromParticipant(participant, track_source, options);
  return stream;
}

VideoStream::VideoStream() : impl_(std::make_unique<Impl>()) {}

VideoStream::~VideoStream() = default;

VideoStream::VideoStream(VideoStream &&other) noexcept = default;

VideoStream &VideoStream::operator=(VideoStream &&other) noexcept = default;

// --------------------- Public API ---------------------

bool VideoStream::read(VideoFrameEvent &out) {
  return impl_ ? impl_->read(out) : false;
}

void VideoStream::close() {
  if (impl_) {
    impl_->close();
  }
}

// --------------------- Internal helpers ---------------------

void VideoStream::initFromTrack(const std::shared_ptr<Track> &track,
                                const Options &options) {
  impl_->initFromTrack(track, options);
}

void VideoStream::initFromParticipant(Participant &participant,
                                      TrackSource track_source,
                                      const Options &options) {
  impl_->initFromParticipant(participant, track_source, options);
}

} // namespace livekit
