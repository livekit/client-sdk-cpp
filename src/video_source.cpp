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
    : width_(width), height_(height), encoded_(true) {
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

VideoSource::~VideoSource() { unregisterEncodedListener(); }

VideoSource::VideoSource(VideoSource&& other) noexcept { *this = std::move(other); }

VideoSource& VideoSource::operator=(VideoSource&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  unregisterEncodedListener();
  handle_ = std::move(other.handle_);
  width_ = other.width_;
  height_ = other.height_;
  encoded_ = other.encoded_;
  {
    std::lock_guard<std::mutex> lock(encoded_observer_lock_);
    std::lock_guard<std::mutex> other_lock(other.encoded_observer_lock_);
    encoded_observer_ = std::move(other.encoded_observer_);
  }
  other.unregisterEncodedListener();
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
  {
    std::lock_guard<std::mutex> lock(encoded_observer_lock_);
    encoded_observer_ = std::move(observer);
  }
  if (encoded_observer_) {
    registerEncodedListener();
  } else {
    unregisterEncodedListener();
  }
}

void VideoSource::registerEncodedListener() {
  if (!encoded_ || !handle_ || encoded_listener_id_ != 0) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(encoded_observer_lock_);
    if (!encoded_observer_) {
      return;
    }
  }
  encoded_listener_id_ =
      FfiClient::instance().AddListener([this](const proto::FfiEvent& event) { handleEncodedEvent(event); });
}

void VideoSource::unregisterEncodedListener() noexcept {
  if (encoded_listener_id_ == 0) {
    return;
  }
  FfiClient::instance().RemoveListener(encoded_listener_id_);
  encoded_listener_id_ = 0;
}

void VideoSource::handleEncodedEvent(const proto::FfiEvent& event) const {
  if (!event.has_encoded_video_source_event()) {
    return;
  }

  const auto& source_event = event.encoded_video_source_event();
  if (source_event.source_handle() != static_cast<std::uint64_t>(handle_.get())) {
    return;
  }

  std::shared_ptr<EncodedVideoSourceObserver> observer;
  {
    std::lock_guard<std::mutex> lock(encoded_observer_lock_);
    observer = encoded_observer_;
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
