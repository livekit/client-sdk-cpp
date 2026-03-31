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

#include "livekit/data_track_error.h"

#include "data_track.pb.h"

namespace livekit {

namespace {

PublishDataTrackErrorCode fromProtoCode(proto::PublishDataTrackErrorCode code) {
  switch (code) {
  case proto::PUBLISH_DATA_TRACK_ERROR_CODE_INVALID_HANDLE:
    return PublishDataTrackErrorCode::INVALID_HANDLE;
  case proto::PUBLISH_DATA_TRACK_ERROR_CODE_DUPLICATE_NAME:
    return PublishDataTrackErrorCode::DUPLICATE_NAME;
  case proto::PUBLISH_DATA_TRACK_ERROR_CODE_TIMEOUT:
    return PublishDataTrackErrorCode::TIMEOUT;
  case proto::PUBLISH_DATA_TRACK_ERROR_CODE_DISCONNECTED:
    return PublishDataTrackErrorCode::DISCONNECTED;
  case proto::PUBLISH_DATA_TRACK_ERROR_CODE_NOT_ALLOWED:
    return PublishDataTrackErrorCode::NOT_ALLOWED;
  case proto::PUBLISH_DATA_TRACK_ERROR_CODE_INVALID_NAME:
    return PublishDataTrackErrorCode::INVALID_NAME;
  case proto::PUBLISH_DATA_TRACK_ERROR_CODE_LIMIT_REACHED:
    return PublishDataTrackErrorCode::LIMIT_REACHED;
  case proto::PUBLISH_DATA_TRACK_ERROR_CODE_PROTOCOL_ERROR:
    return PublishDataTrackErrorCode::PROTOCOL_ERROR;
  case proto::PUBLISH_DATA_TRACK_ERROR_CODE_INTERNAL:
    return PublishDataTrackErrorCode::INTERNAL;
  case proto::PUBLISH_DATA_TRACK_ERROR_CODE_UNKNOWN:
  default:
    return PublishDataTrackErrorCode::UNKNOWN;
  }
}

LocalDataTrackTryPushErrorCode
fromProtoCode(proto::LocalDataTrackTryPushErrorCode code) {
  switch (code) {
  case proto::LOCAL_DATA_TRACK_TRY_PUSH_ERROR_CODE_INVALID_HANDLE:
    return LocalDataTrackTryPushErrorCode::INVALID_HANDLE;
  case proto::LOCAL_DATA_TRACK_TRY_PUSH_ERROR_CODE_TRACK_UNPUBLISHED:
    return LocalDataTrackTryPushErrorCode::TRACK_UNPUBLISHED;
  case proto::LOCAL_DATA_TRACK_TRY_PUSH_ERROR_CODE_QUEUE_FULL:
    return LocalDataTrackTryPushErrorCode::QUEUE_FULL;
  case proto::LOCAL_DATA_TRACK_TRY_PUSH_ERROR_CODE_INTERNAL:
    return LocalDataTrackTryPushErrorCode::INTERNAL;
  case proto::LOCAL_DATA_TRACK_TRY_PUSH_ERROR_CODE_UNKNOWN:
  default:
    return LocalDataTrackTryPushErrorCode::UNKNOWN;
  }
}

SubscribeDataTrackErrorCode
fromProtoCode(proto::SubscribeDataTrackErrorCode code) {
  switch (code) {
  case proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_INVALID_HANDLE:
    return SubscribeDataTrackErrorCode::INVALID_HANDLE;
  case proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_UNPUBLISHED:
    return SubscribeDataTrackErrorCode::UNPUBLISHED;
  case proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_TIMEOUT:
    return SubscribeDataTrackErrorCode::TIMEOUT;
  case proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_DISCONNECTED:
    return SubscribeDataTrackErrorCode::DISCONNECTED;
  case proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_PROTOCOL_ERROR:
    return SubscribeDataTrackErrorCode::PROTOCOL_ERROR;
  case proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_INTERNAL:
    return SubscribeDataTrackErrorCode::INTERNAL;
  case proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_UNKNOWN:
  default:
    return SubscribeDataTrackErrorCode::UNKNOWN;
  }
}

} // namespace

PublishDataTrackError
PublishDataTrackError::fromProto(const proto::PublishDataTrackError &error) {
  return PublishDataTrackError{fromProtoCode(error.code()), error.message()};
}

LocalDataTrackTryPushError LocalDataTrackTryPushError::fromProto(
    const proto::LocalDataTrackTryPushError &error) {
  return LocalDataTrackTryPushError{fromProtoCode(error.code()),
                                    error.message()};
}

SubscribeDataTrackError SubscribeDataTrackError::fromProto(
    const proto::SubscribeDataTrackError &error) {
  return SubscribeDataTrackError{fromProtoCode(error.code()), error.message()};
}

} // namespace livekit
