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
#include <string>

namespace livekit {

/// Categorical reason code for a failed `Room::getStats()` call.
enum class GetSessionStatsErrorCode : std::uint32_t {
  /// Catch-all: the FFI returned an error message that does not map to a more
  /// specific code.
  UNKNOWN = 0,
  /// The `Room` has no live FFI handle (never connected or already
  /// disconnected).
  NOT_CONNECTED = 1,
  /// The FFI responded with an unexpected response shape (e.g. a missing
  /// `get_session_stats` field on the synchronous response).
  PROTOCOL_ERROR = 2,
  /// The FFI threw an internal error while servicing the request (e.g. the
  /// underlying Rust engine reported a failure).
  INTERNAL = 3,
};

/// Typed error returned by `Room::getStats()`.
///
/// Surfaces the error reason as a `GetSessionStatsErrorCode` plus an
/// implementation-defined message for diagnostics/logging.
struct GetSessionStatsError {
  GetSessionStatsErrorCode code{GetSessionStatsErrorCode::UNKNOWN};
  std::string message;
};

} // namespace livekit
