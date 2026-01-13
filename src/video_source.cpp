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

#include <chrono>
#include <stdexcept>

#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/video_frame.h"
#include "video_frame.pb.h"
#include "video_utils.h"

namespace livekit {

VideoSource::VideoSource(int width, int height)
    : width_(width), height_(height) {

  proto::FfiRequest req;
  auto *msg = req.mutable_new_video_source();
  msg->set_type(proto::VideoSourceType::VIDEO_SOURCE_NATIVE);
  msg->mutable_resolution()->set_width(width_);
  msg->mutable_resolution()->set_height(height_);

  auto resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_new_video_source()) {
    throw std::runtime_error("VideoSource: missing new_video_source");
  }

  handle_ = FfiHandle(resp.new_video_source().source().handle().id());
}

void VideoSource::captureFrame(const VideoFrame &frame,
                               std::int64_t timestamp_us,
                               VideoRotation rotation) {
  if (!handle_) {
    return;
  }

  proto::VideoBufferInfo buf = toProto(frame);
  proto::FfiRequest req;
  auto *msg = req.mutable_capture_video_frame();
  msg->set_source_handle(handle_.get());
  msg->mutable_buffer()->CopyFrom(buf);
  msg->set_timestamp_us(timestamp_us);
  msg->set_rotation(static_cast<proto::VideoRotation>(rotation));
  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_capture_video_frame()) {
    throw std::runtime_error("FfiResponse missing capture_video_frame");
  }
}

} // namespace livekit