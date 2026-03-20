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

#include <cstdint>
#include <optional>
#include <vector>

namespace livekit {

/**
 * A single frame of data published or received on a data track.
 *
 * Carries an arbitrary binary payload and an optional user-specified
 * timestamp. The unit is application-defined; the SDK examples use
 * microseconds since the Unix epoch (system_clock).
 */
struct DataFrame {
  /** Arbitrary binary payload (the frame contents). */
  std::vector<std::uint8_t> payload;

  /**
   * Optional application-defined timestamp.
   *
   * The proto field is a bare uint64 with no prescribed unit.
   * By convention the SDK examples use microseconds since the Unix epoch.
   */
  std::optional<std::uint64_t> user_timestamp;
};

} // namespace livekit
