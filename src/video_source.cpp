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

#include "ffi.pb.h"

namespace livekit
{
    VideoSource::VideoSource()
    {
        ConnectRequest *connectRequest = new ConnectRequest;
        FfiRequest request;
        request.mutable_new_video_source()->set_type(VideoSourceType::VIDEO_SOURCE_NATIVE);
        
        FfiResponse response = FfiClient::getInstance().SendRequest(request);
        sourceInfo_ = response.new_video_source().source();
        handle_ = FfiHandle(sourceInfo_.handle().id());
    }

    void VideoSource::CaptureFrame(const VideoFrame& videoFrame) const
    {
        std::cout << "VideoSource::CaptureFrame" << std::endl;
        FfiRequest request;
        CaptureVideoFrameRequest* const captureVideoFrame = request.mutable_capture_video_frame();
        captureVideoFrame->mutable_source_handle()->set_id(handle_.handle);
        captureVideoFrame->mutable_buffer_handle()->set_id(videoFrame.GetBuffer().GetHandle().handle);
        captureVideoFrame->mutable_frame()->set_rotation (videoFrame.GetRotation());
        captureVideoFrame->mutable_frame()->set_timestamp_us(videoFrame.GetTimestamp());
        FfiClient::getInstance().SendRequest(request);
    }
}
