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

#include "livekit/remote_video_track.h"

#include "ffi.pb.h"
#include "livekit/ffi_client.h"
#include "livekit/video_source.h"
#include "track.pb.h"
#include "track_proto_converter.h"

namespace livekit {

RemoteVideoTrack::RemoteVideoTrack(const proto::OwnedTrack &track)
    : Track(FfiHandle{static_cast<uintptr_t>(track.handle().id())},
            track.info().sid(), track.info().name(),
            fromProto(track.info().kind()),
            fromProto(track.info().stream_state()), track.info().muted(),
            true) {}

std::string RemoteVideoTrack::to_string() const {
  return "rtc.RemoteVideoTrack(sid=" + sid() + ", name=" + name() + ")";
}

} // namespace livekit