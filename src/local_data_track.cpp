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

#include "livekit/local_data_track.h"

#include "data_track.pb.h"
#include "data_track_proto_converter.h"
#include "livekit/data_track_error.h"
#include "livekit_ffi.hpp"

namespace livekit {

LocalDataTrack::LocalDataTrack(const proto::OwnedLocalDataTrack& owned)
    : handle_(static_cast<uintptr_t>(owned.handle().id())), info_(fromProto(owned.info())) {}

Result<void, LocalDataTrackTryPushError> LocalDataTrack::tryPush(const DataTrackFrame& frame) {
  return tryPush(frame.payload.data(), frame.payload.size(), frame.user_timestamp);
}

Result<void, LocalDataTrackTryPushError> LocalDataTrack::tryPush(std::vector<std::uint8_t>&& payload,
                                                                 std::optional<std::uint64_t> user_timestamp) {
  const DataTrackFrame frame(std::move(payload), user_timestamp);
  return tryPush(frame);
}

Result<void, LocalDataTrackTryPushError> LocalDataTrack::tryPush(const std::uint8_t* data, std::size_t size,
                                                                 std::optional<std::uint64_t> user_timestamp) {
  if (!handle_.valid()) {
    return Result<void, LocalDataTrackTryPushError>::failure(LocalDataTrackTryPushError{
        LocalDataTrackTryPushErrorCode::INVALID_HANDLE, "LocalDataTrack::tryPush: invalid FFI handle"});
  }
  if (size == 0) {
    return Result<void, LocalDataTrackTryPushError>::success();
  }
  if (data == nullptr) {
    return Result<void, LocalDataTrackTryPushError>::failure(LocalDataTrackTryPushError{
        LocalDataTrackTryPushErrorCode::INTERNAL, "LocalDataTrack::tryPush: payload pointer is null"});
  }

  try {
    livekit_ffi::LocalDataTrack::from_ffi_handle_id(handle_.get())
        ->try_push(livekit_ffi::DataTrackFrame{std::vector<std::uint8_t>(data, data + size), user_timestamp});
    return Result<void, LocalDataTrackTryPushError>::success();
  } catch (const livekit_datatrack::push_frame_error_reason::TrackUnpublished&) {
    return Result<void, LocalDataTrackTryPushError>::failure(LocalDataTrackTryPushError{
        LocalDataTrackTryPushErrorCode::TRACK_UNPUBLISHED, "Failed to publish frame: track unpublished"});
  } catch (const livekit_datatrack::push_frame_error_reason::QueueFull&) {
    return Result<void, LocalDataTrackTryPushError>::failure(
        LocalDataTrackTryPushError{LocalDataTrackTryPushErrorCode::QUEUE_FULL, "Failed to publish frame: queue full"});
  } catch (const std::exception& e) {
    return Result<void, LocalDataTrackTryPushError>::failure(
        LocalDataTrackTryPushError{LocalDataTrackTryPushErrorCode::INTERNAL, e.what()});
  }
}

bool LocalDataTrack::isPublished() const {
  if (!handle_.valid()) {
    return false;
  }

  return livekit_ffi::LocalDataTrack::from_ffi_handle_id(handle_.get())->is_published();
}

void LocalDataTrack::unpublishDataTrack() {
  if (!handle_.valid()) {
    return;
  }

  livekit_ffi::LocalDataTrack::from_ffi_handle_id(handle_.get())->unpublish();
}

} // namespace livekit
