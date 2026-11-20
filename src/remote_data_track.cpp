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

#include "livekit/remote_data_track.h"

#include <stdexcept>

#include "data_track.pb.h"
#include "data_track_proto_converter.h"
#include "livekit_ffi.hpp"
#include "lk_log.h"

namespace livekit {

RemoteDataTrack::RemoteDataTrack(const proto::OwnedRemoteDataTrack& owned)
    : handle_(static_cast<uintptr_t>(owned.handle().id())), publisher_identity_(owned.publisher_identity()) {
  info_ = fromProto(owned.info());
}

bool RemoteDataTrack::isPublished() const {
  if (!handle_.valid()) {
    return false;
  }

  return livekit_ffi::RemoteDataTrack::from_ffi_handle_id(handle_.get())->is_published();
}

void RemoteDataTrack::setPipelineOptions(const DataTrackPipelineOptions& options) {
  if (!handle_.valid()) {
    return;
  }

  livekit_ffi::RemoteDataTrack::from_ffi_handle_id(handle_.get())
      ->set_pipeline_options(livekit_ffi::RemoteDataTrackPipelineOptions{options.max_partial_frames});
}

Result<std::shared_ptr<DataTrackStream>, SubscribeDataTrackError> RemoteDataTrack::subscribe(
    const DataTrackStream::Options& options) {
  if (!handle_.valid()) {
    LK_LOG_WARN("Subscribe data track failed: code={} message={}",
                static_cast<std::uint32_t>(SubscribeDataTrackErrorCode::INVALID_HANDLE),
                "RemoteDataTrack::subscribe: invalid FFI handle");
    return Result<std::shared_ptr<DataTrackStream>, SubscribeDataTrackError>::failure(
        SubscribeDataTrackError{SubscribeDataTrackErrorCode::INVALID_HANDLE,
                                "RemoteDataTrack::subscribe: invalid FFI "
                                "handle"});
  }

  try {
    auto stream = std::shared_ptr<DataTrackStream>(new DataTrackStream());
    stream->init(static_cast<std::uint64_t>(handle_.get()), options);
    return Result<std::shared_ptr<DataTrackStream>, SubscribeDataTrackError>::success(std::move(stream));
  } catch (const std::exception& error) {
    return Result<std::shared_ptr<DataTrackStream>, SubscribeDataTrackError>::failure(
        SubscribeDataTrackError{SubscribeDataTrackErrorCode::INTERNAL, error.what()});
  }
}

} // namespace livekit
