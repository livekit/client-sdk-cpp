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

#include "livekit/ffi_client.h"
#include "livekit/participant.h"
#include "livekit_ffi.h"

#include <mutex>
#include "ffi.pb.h"
#include "room.pb.h"

namespace livekit {
    class LocalParticipant;
    class Room : public std::enable_shared_from_this<Room>
    {
    public:
        Room();
        ~Room();
        void Connect(const std::string& url, const std::string& token);
        void OnTrackPublished(const std::string& name, const std::string& sid, const std::string& inputTrackSid);
        
        FfiHandle GetHandle() const { return handle_; }
        const std::string& GetName() const { return roomInfo_.name(); }
        const std::string& GetSid() const { return roomInfo_.sid(); }
        const std::string& GetMetadata() const { return roomInfo_.metadata(); }
        bool IsConnected() const { return handle_.GetHandle() != INVALID_HANDLE; }

    private:
        // mutable std::mutex lock_;
        FfiHandle handle_{INVALID_HANDLE};
        RoomInfo roomInfo_;
        uint64_t connectAsyncId_{0};
        std::shared_ptr<LocalParticipant> localParticipant_{nullptr};
        FfiClient::ListenerId eventListenerId_;

        void OnEvent(const FfiEvent& event);
    };
}

#endif /* LIVEKIT_ROOM_H */
