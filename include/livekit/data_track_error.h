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

#pragma once

#include <cstdint>
#include <string>

namespace livekit {

namespace proto {
class PublishDataTrackError;
class LocalDataTrackTryPushError;
class SubscribeDataTrackError;
} // namespace proto

enum class PublishDataTrackErrorCode : std::uint32_t {
  UNKNOWN = 0,
  INVALID_HANDLE = 1,
  DUPLICATE_NAME = 2,
  TIMEOUT = 3,
  DISCONNECTED = 4,
  NOT_ALLOWED = 5,
  INVALID_NAME = 6,
  LIMIT_REACHED = 7,
  PROTOCOL_ERROR = 8,
  INTERNAL = 9,
};

struct PublishDataTrackError {
  PublishDataTrackErrorCode code{PublishDataTrackErrorCode::UNKNOWN};
  std::string message;

  static PublishDataTrackError
  fromProto(const proto::PublishDataTrackError &error);
};

enum class LocalDataTrackTryPushErrorCode : std::uint32_t {
  UNKNOWN = 0,
  INVALID_HANDLE = 1,
  TRACK_UNPUBLISHED = 2,
  QUEUE_FULL = 3,
  INTERNAL = 4,
};

struct LocalDataTrackTryPushError {
  LocalDataTrackTryPushErrorCode code{LocalDataTrackTryPushErrorCode::UNKNOWN};
  std::string message;

  static LocalDataTrackTryPushError
  fromProto(const proto::LocalDataTrackTryPushError &error);
};

enum class SubscribeDataTrackErrorCode : std::uint32_t {
  UNKNOWN = 0,
  INVALID_HANDLE = 1,
  UNPUBLISHED = 2,
  TIMEOUT = 3,
  DISCONNECTED = 4,
  PROTOCOL_ERROR = 5,
  INTERNAL = 6,
};

struct SubscribeDataTrackError {
  SubscribeDataTrackErrorCode code{SubscribeDataTrackErrorCode::UNKNOWN};
  std::string message;

  static SubscribeDataTrackError
  fromProto(const proto::SubscribeDataTrackError &error);
};

} // namespace livekit
