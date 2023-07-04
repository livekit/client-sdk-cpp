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

#ifndef LIVEKIT_VIDEO_SOURCE_H
#define LIVEKIT_VIDEO_SOURCE_H

#include "livekit/video_frame.h"
#include "livekit/ffi_client.h"
#include "livekit_ffi.h"

#include "ffi.pb.h"

namespace livekit
{
    class VideoSource
    {
    public:
        VideoSource();

        void CaptureFrame(const VideoFrame& videoFrame) const;
        const FfiHandle& GetHandle() const { return handle_; }

    private:
        FfiHandle handle_{INVALID_HANDLE};
        livekit::VideoSourceInfo sourceInfo_;
    };
}

#endif /* LIVEKIT_VIDEO_SOURCE_H */
