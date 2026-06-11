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

#include "livekit/video_source.h"

#include <mutex>
#include <stdexcept>
#include <utility>

#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/video_frame.h"
#include "video_frame.pb.h"
#include "video_utils.h"

namespace livekit {

namespace {

proto::VideoCodec toProto(VideoCodec codec) { return static_cast<proto::VideoCodec>(codec); }

} // namespace

struct VideoSource::EncodedListenerState {
  void reset() {
    std::scoped_lock const guard(lock);
    active = false;
    source_handle = 0;
    observer.reset();
  }

  mutable std::mutex lock;
  std::uint64_t source_handle{0};
  bool active{false};
  std::shared_ptr<EncodedVideoSourceObserver> observer;
};

VideoSource::VideoSource(int width, int height) : width_(width), height_(height) {
  proto::FfiRequest req;
  auto* msg = req.mutable_new_video_source();
  msg->set_type(proto::VideoSourceType::VIDEO_SOURCE_NATIVE);
  msg->mutable_resolution()->set_width(width_);
  msg->mutable_resolution()->set_height(height_);

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_new_video_source()) {
    throw std::runtime_error("VideoSource: missing new_video_source");
  }

  handle_ = FfiHandle(resp.new_video_source().source().handle().id());
}

VideoSource::VideoSource(int width, int height, const EncodedVideoSourceOptions& encoded_options)
    : width_(width),
      height_(height),
      encoded_(true),
      encoded_listener_state_(std::make_shared<EncodedListenerState>()) {
  proto::FfiRequest req;
  auto* msg = req.mutable_new_video_source();
  msg->set_type(proto::VideoSourceType::VIDEO_SOURCE_ENCODED);
  msg->mutable_resolution()->set_width(width_);
  msg->mutable_resolution()->set_height(height_);
  msg->mutable_encoded_options()->set_codec(toProto(encoded_options.codec));

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_new_video_source()) {
    throw std::runtime_error("VideoSource: missing new_video_source");
  }

  handle_ = FfiHandle(resp.new_video_source().source().handle().id());
}

VideoSource::~VideoSource() {
  if (encoded_listener_state_) {
    encoded_listener_state_->reset();
  }
  unregisterEncodedListener();
}

VideoSource::VideoSource(VideoSource&& other) noexcept { *this = std::move(other); }

VideoSource& VideoSource::operator=(VideoSource&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  unregisterEncodedListener();
  other.unregisterEncodedListener();
  handle_ = std::move(other.handle_);
  width_ = other.width_;
  height_ = other.height_;
  encoded_ = other.encoded_;
  if (encoded_listener_state_ && other.encoded_listener_state_) {
    std::scoped_lock const lock(encoded_listener_state_->lock, other.encoded_listener_state_->lock);
    encoded_listener_state_->observer = std::move(other.encoded_listener_state_->observer);
    encoded_listener_state_->source_handle = handle_.get();
    encoded_listener_state_->active = false;
    other.encoded_listener_state_->source_handle = 0;
    other.encoded_listener_state_->active = false;
  } else if (encoded_listener_state_) {
    encoded_listener_state_->reset();
  } else {
    encoded_listener_state_ = std::move(other.encoded_listener_state_);
  }
  other.encoded_ = false;
  registerEncodedListener();
  return *this;
}

void VideoSource::captureFrame(const VideoFrame& frame, const VideoCaptureOptions& options) {
  if (!handle_) {
    return;
  }

  const proto::VideoBufferInfo buf = toProto(frame);
  proto::FfiRequest req;
  auto* msg = req.mutable_capture_video_frame();
  msg->set_source_handle(handle_.get());
  msg->mutable_buffer()->CopyFrom(buf);
  msg->set_timestamp_us(options.timestamp_us);
  msg->set_rotation(static_cast<proto::VideoRotation>(options.rotation));
  if (auto metadata = toProto(options.metadata)) {
    msg->mutable_metadata()->CopyFrom(*metadata);
  }
  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_capture_video_frame()) {
    throw std::runtime_error("FfiResponse missing capture_video_frame");
  }
}

void VideoSource::captureFrame(const VideoFrame& frame, std::int64_t timestamp_us, VideoRotation rotation) {
  captureFrame(frame, VideoCaptureOptions{timestamp_us, rotation, {}});
}

bool VideoSource::captureEncodedFrame(const std::uint8_t* data, std::size_t size, const EncodedVideoFrameInfo& info) {
  if (!handle_) {
    return false;
  }
  if (!encoded_) {
    throw std::runtime_error("captureEncodedFrame requires an encoded VideoSource");
  }
  if (data == nullptr && size != 0) {
    throw std::invalid_argument("captureEncodedFrame data is null");
  }

  proto::FfiRequest req;
  auto* msg = req.mutable_capture_encoded_video_frame();
  msg->set_source_handle(handle_.get());
  msg->set_data(data, size);
  msg->set_is_keyframe(info.is_keyframe);
  msg->set_has_sps_pps(info.has_sps_pps);
  msg->set_width(info.width);
  msg->set_height(info.height);
  msg->set_capture_time_us(info.capture_time_us);

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_capture_encoded_video_frame()) {
    throw std::runtime_error("FfiResponse missing capture_encoded_video_frame");
  }
  return resp.capture_encoded_video_frame().accepted();
}

void VideoSource::setEncodedObserver(std::shared_ptr<EncodedVideoSourceObserver> observer) {
  if (!encoded_) {
    throw std::runtime_error("setEncodedObserver requires an encoded VideoSource");
  }
  if (!encoded_listener_state_) {
    encoded_listener_state_ = std::make_shared<EncodedListenerState>();
  }
  bool has_observer = false;
  {
    std::scoped_lock const lock(encoded_listener_state_->lock);
    encoded_listener_state_->observer = std::move(observer);
    has_observer = encoded_listener_state_->observer != nullptr;
  }
  if (has_observer) {
    registerEncodedListener();
  } else {
    unregisterEncodedListener();
  }
}

void VideoSource::registerEncodedListener() {
  if (!encoded_ || !handle_ || encoded_listener_id_ != 0) {
    return;
  }
  if (!encoded_listener_state_) {
    encoded_listener_state_ = std::make_shared<EncodedListenerState>();
  }
  {
    std::scoped_lock const lock(encoded_listener_state_->lock);
    if (!encoded_listener_state_->observer) {
      return;
    }
    encoded_listener_state_->source_handle = handle_.get();
    encoded_listener_state_->active = true;
  }
  const std::weak_ptr<EncodedListenerState> state(encoded_listener_state_);
  encoded_listener_id_ = FfiClient::instance().addListener(
      [state](const proto::FfiEvent& event) { VideoSource::handleEncodedEvent(state, event); });
}

void VideoSource::unregisterEncodedListener() noexcept {
  if (encoded_listener_state_) {
    std::scoped_lock const lock(encoded_listener_state_->lock);
    encoded_listener_state_->active = false;
    encoded_listener_state_->source_handle = 0;
  }
  if (encoded_listener_id_ == 0) {
    return;
  }
  FfiClient::instance().removeListener(encoded_listener_id_);
  encoded_listener_id_ = 0;
}

void VideoSource::handleEncodedEvent(const std::weak_ptr<EncodedListenerState>& weak_state,
                                     const proto::FfiEvent& event) {
  if (!event.has_encoded_video_source_event()) {
    return;
  }
  const auto state = weak_state.lock();
  if (!state) {
    return;
  }

  const auto& source_event = event.encoded_video_source_event();
  std::shared_ptr<EncodedVideoSourceObserver> observer;
  {
    std::scoped_lock const lock(state->lock);
    if (!state->active || source_event.source_handle() != state->source_handle) {
      return;
    }
    observer = state->observer;
  }
  if (!observer) {
    return;
  }

  switch (source_event.message_case()) {
    case proto::EncodedVideoSourceEvent::kKeyframeRequested:
      observer->onKeyframeRequested();
      break;
    case proto::EncodedVideoSourceEvent::kTargetBitrateChanged:
      observer->onTargetBitrate(source_event.target_bitrate_changed().bitrate_bps(),
                                source_event.target_bitrate_changed().framerate_fps());
      break;
    case proto::EncodedVideoSourceEvent::MESSAGE_NOT_SET:
    default:
      break;
  }
}

} // namespace livekit
