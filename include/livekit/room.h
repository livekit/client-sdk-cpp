/*
 * Copyright 2023 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the “License”);
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

#include <mutex>

#include "ffi.pb.h"
#include "livekit/ffi_client.h"
#include "livekit/participant.h"
#include "livekit_ffi.h"
#include "room.pb.h"

namespace livekit {
class LocalParticipant;
class Room {
 public:
  Room();
  ~Room();
  void Connect(const std::string& url, const std::string& token);
  void OnTrackPublished(const std::string& name,
                        const std::string& sid,
                        const std::string& inputTrackSid);

  const std::string& GetName() const { return info_.name(); }
  const std::string& GetSid() const { return info_.sid(); }
  const std::string& GetMetadata() const { return info_.metadata(); }
  bool IsConnected() const { return handle_.GetHandle() != INVALID_HANDLE; }
  std::shared_ptr<LocalParticipant> GetLocalParticipant() const {
    return localParticipant_;
  }

 private:
  friend LocalParticipant;
  // mutable std::mutex lock_;
  FfiHandle handle_{INVALID_HANDLE};
  std::shared_ptr<LocalParticipant> localParticipant_{nullptr};
  proto::RoomInfo info_;

  uint64_t connectAsyncId_{0};
  FfiClient::ListenerId listenerId_;

  void OnEvent(const proto::FfiEvent& event);
};
}  // namespace livekit

#endif /* LIVEKIT_ROOM_H */
