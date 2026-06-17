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

#include <optional>
#include <string>

#include "livekit/data_track_schema.h"

namespace livekit {

/**
 * Options for publishing a data track.
 *
 * The schema and frame encoding are optional metadata advertised to
 * subscribers; they are surfaced on the subscriber side via DataTrackInfo.
 */
struct DataTrackPublishOptions {
  /// Track name used to identify the track to other participants.
  ///
  /// Must not be empty and must be unique per publisher.
  std::string name;

  /// Schema describing frames sent on the track, if any.
  std::optional<DataTrackSchemaId> schema;

  /// Encoding of frames sent on the track, if any.
  std::optional<DataTrackFrameEncoding> frame_encoding;
};

} // namespace livekit
