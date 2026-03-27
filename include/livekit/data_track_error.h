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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LIVEKIT_DATA_TRACK_ERROR_H
#define LIVEKIT_DATA_TRACK_ERROR_H

#include <cstdint>
#include <string>

namespace livekit {

namespace proto {
class DataTrackError;
}

/// Stable error codes for data-track operations.
enum class DataTrackErrorCode : std::uint32_t {
  UNKNOWN = 0,
  INVALID_HANDLE = 1,
  DUPLICATE_TRACK_NAME = 2,
  TRACK_UNPUBLISHED = 3,
  BUFFER_FULL = 4,
  SUBSCRIPTION_CLOSED = 5,
  CANCELLED = 6,
  PROTOCOL_ERROR = 7,
  INTERNAL = 8,
};

/// Structured failure returned by non-throwing data-track APIs.
struct DataTrackError {
  /// Machine-readable error code.
  DataTrackErrorCode code{DataTrackErrorCode::UNKNOWN};
  /// Human-readable description from the backend or SDK.
  std::string message;
  /// Whether retrying the operation may succeed.
  bool retryable{false};

  /// Convert the FFI proto representation into the public SDK type.
  static DataTrackError fromProto(const proto::DataTrackError &error);
};

} // namespace livekit

#endif // LIVEKIT_DATA_TRACK_ERROR_H