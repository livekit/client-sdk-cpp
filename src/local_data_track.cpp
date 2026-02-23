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

#include "livekit/local_data_track.h"

#include "data_track.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"

namespace livekit {

LocalDataTrack::LocalDataTrack(const proto::OwnedLocalDataTrack &owned)
    : handle_(static_cast<uintptr_t>(owned.handle().id())) {
  const auto &pi = owned.info();
  info_.name = pi.name();
  info_.sid = pi.sid();
  info_.uses_e2ee = pi.uses_e2ee();
}

bool LocalDataTrack::tryPush(const DataTrackFrame &frame) {
  if (!handle_.valid()) {
    return false;
  }

  proto::FfiRequest req;
  auto *msg = req.mutable_local_data_track_try_push();
  msg->set_track_handle(static_cast<uint64_t>(handle_.get()));
  auto *pf = msg->mutable_frame();
  pf->set_payload(frame.payload.data(), frame.payload.size());
  if (frame.user_timestamp.has_value()) {
    pf->set_user_timestamp(frame.user_timestamp.value());
  }

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  const auto &r = resp.local_data_track_try_push();
  return !r.has_error();
}

bool LocalDataTrack::isPublished() const {
  if (!handle_.valid()) {
    return false;
  }

  proto::FfiRequest req;
  auto *msg = req.mutable_local_data_track_is_published();
  msg->set_track_handle(static_cast<uint64_t>(handle_.get()));

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  return resp.local_data_track_is_published().is_published();
}

void LocalDataTrack::unpublish() {
  if (!handle_.valid()) {
    return;
  }

  proto::FfiRequest req;
  auto *msg = req.mutable_local_data_track_unpublish();
  msg->set_track_handle(static_cast<uint64_t>(handle_.get()));

  (void)FfiClient::instance().sendRequest(req);
}

} // namespace livekit
