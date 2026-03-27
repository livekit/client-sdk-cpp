#include "livekit/data_track_error.h"

#include "data_track.pb.h"

namespace livekit {

namespace {

DataTrackErrorCode fromProtoCode(proto::DataTrackErrorCode code) {
  switch (code) {
  case proto::DATA_TRACK_ERROR_CODE_INVALID_HANDLE:
    return DataTrackErrorCode::INVALID_HANDLE;
  case proto::DATA_TRACK_ERROR_CODE_DUPLICATE_TRACK_NAME:
    return DataTrackErrorCode::DUPLICATE_TRACK_NAME;
  case proto::DATA_TRACK_ERROR_CODE_TRACK_UNPUBLISHED:
    return DataTrackErrorCode::TRACK_UNPUBLISHED;
  case proto::DATA_TRACK_ERROR_CODE_BUFFER_FULL:
    return DataTrackErrorCode::BUFFER_FULL;
  case proto::DATA_TRACK_ERROR_CODE_SUBSCRIPTION_CLOSED:
    return DataTrackErrorCode::SUBSCRIPTION_CLOSED;
  case proto::DATA_TRACK_ERROR_CODE_CANCELLED:
    return DataTrackErrorCode::CANCELLED;
  case proto::DATA_TRACK_ERROR_CODE_PROTOCOL_ERROR:
    return DataTrackErrorCode::PROTOCOL_ERROR;
  case proto::DATA_TRACK_ERROR_CODE_INTERNAL:
    return DataTrackErrorCode::INTERNAL;
  case proto::DATA_TRACK_ERROR_CODE_UNKNOWN:
  default:
    return DataTrackErrorCode::UNKNOWN;
  }
}

} // namespace

DataTrackError DataTrackError::fromProto(const proto::DataTrackError &error) {
  return DataTrackError{fromProtoCode(error.code()), error.message(),
                        error.retryable()};
}

} // namespace livekit
