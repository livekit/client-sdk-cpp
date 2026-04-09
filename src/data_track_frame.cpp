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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "livekit/data_track_frame.h"

#include "data_track.pb.h"

namespace livekit {

DataTrackFrame
DataTrackFrame::fromOwnedInfo(const proto::DataTrackFrame &owned) {
  DataTrackFrame frame;
  const auto &payload_str = owned.payload();
  frame.payload.assign(
      reinterpret_cast<const std::uint8_t *>(payload_str.data()),
      reinterpret_cast<const std::uint8_t *>(payload_str.data()) +
          payload_str.size());
  if (owned.has_user_timestamp()) {
    frame.user_timestamp = owned.user_timestamp();
  }
  return frame;
}

} // namespace livekit
