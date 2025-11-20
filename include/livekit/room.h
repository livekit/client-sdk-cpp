/*
 * Copyright 2023 LiveKit
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

#ifndef LIVEKIT_ROOM_H
#define LIVEKIT_ROOM_H

#include "livekit/ffi_client.h"
#include "livekit/ffi_handle.h"
#include "livekit/room_delegate.h"
#include <mutex>

namespace livekit {

class RoomDelegate;
struct RoomInfoData;
namespace proto {
class FfiEvent;
}

class Room {
public:
  Room() = default;
  void setDelegate(RoomDelegate *delegate);
  bool Connect(const std::string &url, const std::string &token);

  // Accessors
  RoomInfoData room_info() const;

private:
  mutable std::mutex lock_;
  bool connected_{false};
  RoomDelegate *delegate_ = nullptr; // Not owned
  RoomInfoData room_info_;

  void OnEvent(const proto::FfiEvent &event);
};
} // namespace livekit

#endif /* LIVEKIT_ROOM_H */
