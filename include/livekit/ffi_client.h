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

#ifndef LIVEKIT_H
#define LIVEKIT_H

#include <iostream>
#include <memory>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "ffi.pb.h"

namespace livekit
{
    extern "C" void LivekitFfiCallback(const uint8_t *buf, size_t len);

    // The FfiClient is used to communicate with the FFI interface of the Rust SDK
    // We use the generated protocol messages to facilitate the communication
    class FfiClient
    {
    public:
        using ListenerId = int;
        using Listener = std::function<void(const FFIEvent&)>;

        FfiClient(const FfiClient&) = delete;
        FfiClient& operator=(const FfiClient&) = delete;

        static FfiClient& getInstance() {
            static FfiClient instance;
            return instance;
        }
 
        ListenerId AddListener(const Listener& listener);
        void RemoveListener(ListenerId id);

        FFIResponse SendRequest(const FFIRequest& request)const;

    private:
        std::unordered_map<ListenerId, Listener> listeners_;
        ListenerId nextListenerId = 1;
        mutable std::mutex lock_;

        FfiClient();
        ~FfiClient() = default;

        void PushEvent(const FFIEvent& event) const;
        friend void LivekitFfiCallback(const uint8_t *buf, size_t len);
    };

}

#endif /* LIVEKIT_H */
