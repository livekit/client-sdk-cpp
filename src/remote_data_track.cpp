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

#include "data_track.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"

#include <stdexcept>

namespace livekit {

RemoteDataTrack::RemoteDataTrack(const proto::OwnedRemoteDataTrack &owned)
    : handle_(static_cast<uintptr_t>(owned.handle().id())),
      publisher_identity_(owned.publisher_identity()) {
  const auto &pi = owned.info();
  info_.name = pi.name();
  info_.sid = pi.sid();
  info_.uses_e2ee = pi.uses_e2ee();
}

bool RemoteDataTrack::isPublished() const {
  if (!handle_.valid()) {
    return false;
  }

  proto::FfiRequest req;
  auto *msg = req.mutable_remote_data_track_is_published();
  msg->set_track_handle(static_cast<uint64_t>(handle_.get()));

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  return resp.remote_data_track_is_published().is_published();
}

Result<std::shared_ptr<DataTrackSubscription>, SubscribeDataTrackError>
RemoteDataTrack::subscribe(const DataTrackSubscription::Options &options) {
  if (!handle_.valid()) {
    return Result<std::shared_ptr<DataTrackSubscription>,
                  SubscribeDataTrackError>::failure(SubscribeDataTrackError{
        SubscribeDataTrackErrorCode::INVALID_HANDLE,
        "RemoteDataTrack::subscribe: invalid FFI "
        "handle"});
  }

  auto result = FfiClient::instance().subscribeDataTrack(
      static_cast<std::uint64_t>(handle_.get()), options.buffer_size);
  if (!result) {
    return Result<std::shared_ptr<DataTrackSubscription>,
                  SubscribeDataTrackError>::failure(std::move(result).error());
  }

  proto::OwnedDataTrackSubscription owned_sub = result.value();

  FfiHandle sub_handle(static_cast<uintptr_t>(owned_sub.handle().id()));

  auto subscription =
      std::shared_ptr<DataTrackSubscription>(new DataTrackSubscription());
  subscription->init(std::move(sub_handle));
  return Result<std::shared_ptr<DataTrackSubscription>,
                SubscribeDataTrackError>::success(std::move(subscription));
}

} // namespace livekit
