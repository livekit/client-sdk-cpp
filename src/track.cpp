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

#include "livekit/track.h"

#include "livekit/video_source.h"

namespace livekit {
    std::unique_ptr<LocalVideoTrack> LocalVideoTrack::CreateVideoTrack(const std::string& name, const VideoSource& source) {
        FfiRequest request;
        request.mutable_create_video_track()->set_name(name);
        request.mutable_create_video_track()->mutable_source_handle()->set_id(source.GetHandle().handle);

        FfiResponse resp = FfiClient::getInstance().SendRequest(request);
        TrackInfo trackInfo = resp.create_video_track().track();
        FfiHandle ffiHandle = FfiHandle(trackInfo.handle().id());
        return std::make_unique<LocalVideoTrack>(ffiHandle, trackInfo);
    }

}
