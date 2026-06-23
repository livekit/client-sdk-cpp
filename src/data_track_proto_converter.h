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

namespace livekit {

proto::DataTrackSchemaEncoding toProto(DataTrackSchemaEncoding in);
DataTrackSchemaEncoding fromProto(proto::DataTrackSchemaEncoding in);

proto::DataTrackFrameEncoding toProto(DataTrackFrameEncoding in);
DataTrackFrameEncoding fromProto(proto::DataTrackFrameEncoding in);

proto::DataTrackSchemaId toProto(const DataTrackSchemaId& in);
DataTrackSchemaId fromProto(const proto::DataTrackSchemaId& in);

// Converts an FFI data track info message into the public DataTrackInfo struct.
DataTrackInfo fromProto(const proto::DataTrackInfo& in);

} // namespace livekit
