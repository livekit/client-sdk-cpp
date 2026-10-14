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

#include <livekit/livekit.h>

#include <memory>
#include <mutex>
#include <stdexcept>

#include "ffi_client.h"
#include "room.pb.h"

namespace livekit {

struct RoomTestAccess {
  static int listenerId(const Room& room) {
    const std::scoped_lock<std::mutex> guard(room.lock_);
    return room.listener_id_;
  }

  static void simulateScenario(Room& room, proto::SimulateScenarioKind scenario) {
    std::shared_ptr<FfiHandle> handle;
    {
      const std::scoped_lock<std::mutex> guard(room.lock_);
      handle = room.room_handle_;
    }
    if (!handle) {
      throw std::runtime_error("cannot simulate scenario for a disconnected room");
    }
    FfiClient::instance().simulateScenarioAsync(handle->get(), static_cast<int>(scenario)).get();
  }
};

} // namespace livekit
