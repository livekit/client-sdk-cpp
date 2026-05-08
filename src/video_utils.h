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

#include "livekit/export.h"
#include "livekit/video_frame.h"
#include "livekit/video_source.h"
#include "video_frame.pb.h"

namespace livekit {

// Video FFI Utils.
// Tagged LIVEKIT_INTERNAL_API: not part of the public ABI, but exposed so
// in-tree tests that include this header can link.
LIVEKIT_INTERNAL_API proto::VideoBufferInfo toProto(const VideoFrame& frame);
LIVEKIT_INTERNAL_API VideoFrame fromOwnedProto(const proto::OwnedVideoBuffer& owned);
LIVEKIT_INTERNAL_API VideoFrame convertViaFfi(const VideoFrame& frame, VideoBufferType dst, bool flip_y);
LIVEKIT_INTERNAL_API proto::VideoBufferType toProto(const VideoBufferType t);
LIVEKIT_INTERNAL_API VideoBufferType fromProto(const proto::VideoBufferType t);
LIVEKIT_INTERNAL_API std::optional<proto::FrameMetadata> toProto(const std::optional<VideoFrameMetadata>& metadata);
LIVEKIT_INTERNAL_API std::optional<VideoFrameMetadata> fromProto(const proto::FrameMetadata& metadata);

} // namespace livekit
