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
#include "livekit_ffi.h"

namespace livekit
{
    class Room
    {
    public:
        void Connect(const std::string& url, const std::string& token);

    private:
        void OnConnect(const proto::ConnectCallback& cb);

        mutable std::mutex lock_;
        FfiHandle handle_{INVALID_HANDLE};
        bool connected_{false};
        uint64_t connectAsyncId_{0};
        

        void OnEvent(const proto::FfiEvent& event);
    };
}

#endif /* LIVEKIT_ROOM_H */
