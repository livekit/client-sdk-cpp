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

#include <livekit/data_track_info.h>
#include <livekit/remote_data_track.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "data_track.pb.h"

namespace livekit {

struct RemoteDataTrackTestAccess {
  static uintptr_t ffiHandleId(const RemoteDataTrack& track) noexcept { return track.ffiHandleId(); }

  static std::shared_ptr<RemoteDataTrack> create(DataTrackInfo info, std::string publisher_identity) {
    proto::OwnedRemoteDataTrack owned;
    owned.mutable_handle()->set_id(0);
    auto* proto_info = owned.mutable_info();
    proto_info->set_name(std::move(info.name));
    proto_info->set_sid(std::move(info.sid));
    proto_info->set_uses_e2ee(info.uses_e2ee);
    owned.set_publisher_identity(std::move(publisher_identity));
    return std::shared_ptr<RemoteDataTrack>(new RemoteDataTrack(owned));
  }
};

} // namespace livekit
