/*
 * Copyright 2023 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the “License”);
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

#include <iostream>

#include "ffi.pb.h"
#include "video_frame.pb.h"

namespace livekit {
VideoSource::VideoSource() {
  std::cout << "VideoSource::VideoSource" << std::endl;
  proto::ConnectRequest* connectRequest = new proto::ConnectRequest;
  proto::FfiRequest request{};
  request.mutable_new_video_source()->set_type(
      proto::VideoSourceType::VIDEO_SOURCE_NATIVE);

  proto::FfiResponse response = FfiClient::getInstance().SendRequest(request);
  info_ = response.new_video_source().source();
  handle_ = FfiHandle(info_.handle().id());
}

void VideoSource::CaptureFrame(const VideoFrame& videoFrame) const {
  proto::FfiRequest request{};
  proto::CaptureVideoFrameRequest* captureVideoFrame =
      request.mutable_capture_video_frame();
  captureVideoFrame->mutable_source_handle()->set_id(handle_.GetHandle());
  captureVideoFrame->mutable_buffer_handle()->set_id(
      videoFrame.GetBuffer().GetHandle().GetHandle());
  captureVideoFrame->mutable_frame()->set_rotation(proto::VIDEO_ROTATION_0);
  captureVideoFrame->mutable_frame()->set_timestamp_us(0);
  FfiClient::getInstance().SendRequest(request);
}
}  // namespace livekit
