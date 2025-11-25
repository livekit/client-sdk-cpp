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

#pragma once

#include "livekit/video_frame.h"
#include "video_frame.pb.h"

namespace livekit {

// Video FFI Utils
proto::VideoBufferInfo toProto(const LKVideoFrame &frame);
LKVideoFrame fromOwnedProto(const proto::OwnedVideoBuffer &owned);
LKVideoFrame convertViaFfi(const LKVideoFrame &frame, VideoBufferType dst,
                           bool flip_y);
proto::VideoBufferType toProto(VideoBufferType t);
VideoBufferType fromProto(proto::VideoBufferType t);

} // namespace livekit
