/*
 * Copyright 2026 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "data_track.pb.h"
#include "livekit/data_track_info.h"
#include "livekit/data_track_schema.h"
#include "livekit/visibility.h"

namespace livekit {

LIVEKIT_INTERNAL_API proto::DataTrackSchemaEncoding toProto(const DataTrackSchemaEncoding& in);
LIVEKIT_INTERNAL_API DataTrackSchemaEncoding fromProto(const proto::DataTrackSchemaEncoding& in);

LIVEKIT_INTERNAL_API proto::DataTrackFrameEncoding toProto(const DataTrackFrameEncoding& in);
LIVEKIT_INTERNAL_API DataTrackFrameEncoding fromProto(const proto::DataTrackFrameEncoding& in);

LIVEKIT_INTERNAL_API proto::DataTrackSchemaId toProto(const DataTrackSchemaId& in);
LIVEKIT_INTERNAL_API DataTrackSchemaId fromProto(const proto::DataTrackSchemaId& in);

// Converts an FFI data track info message into the public DataTrackInfo struct.
LIVEKIT_INTERNAL_API DataTrackInfo fromProto(const proto::DataTrackInfo& in);

} // namespace livekit
