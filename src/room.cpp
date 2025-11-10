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

#include "livekit/room.h"
#include "livekit/ffi_client.h"

#include "ffi.pb.h"
#include "room.pb.h"
#include <functional>
#include <iostream>

namespace livekit
{

using proto::FfiRequest;
using proto::FfiResponse;
using proto::ConnectRequest;
using proto::RoomOptions;
using proto::ConnectCallback;
using proto::FfiEvent;

void Room::Connect(const std::string& url, const std::string& token) {
    // Register listener first (outside Room lock to avoid lock inversion)
    auto listenerId = FfiClient::getInstance().AddListener(
        std::bind(&Room::OnEvent, this, std::placeholders::_1));

    // Build request without heap allocs
    livekit::proto::FfiRequest req;
    auto* connect = req.mutable_connect();
    connect->set_url(url);
    connect->set_token(token);
    connect->mutable_options()->set_auto_subscribe(true);

    // Mark “connecting” under lock, but DO NOT keep the lock across SendRequest
    {
        std::lock_guard<std::mutex> g(lock_);
        if (connected_) {
            FfiClient::getInstance().RemoveListener(listenerId);
            throw std::runtime_error("already connected");
        }
        connectAsyncId_ = listenerId;
    }

    // Call into FFI with no Room lock held (avoid re-entrancy deadlock)
    livekit::proto::FfiResponse resp = FfiClient::getInstance().SendRequest(req);
    // Store async id under lock
    {
        std::lock_guard<std::mutex> g(lock_);
        connectAsyncId_ = resp.connect().async_id();
    }
}

void Room::OnEvent(const FfiEvent& event) {
    // TODO, it is not a good idea to lock all the callbacks, improve it.
    std::lock_guard<std::mutex> guard(lock_);
    switch (event.message_case()) {
        case FfiEvent::kConnect:
            OnConnect(event.connect());
            break;

        // TODO: Handle other FfiEvent types here (e.g. room_event, track_event, etc.)
        default:
            break;
    }
}

void Room::OnConnect(const ConnectCallback& cb) {
    // Match the async_id with the pending connectAsyncId_
    if (cb.async_id() != connectAsyncId_) {
        return;
    }

    std::cout << "Received ConnectCallback" << std::endl;

    if (cb.message_case() == ConnectCallback::kError) {
        std::cerr << "Failed to connect to room: " << cb.error() << std::endl;
        connected_ = false;
        return;
    }

    // Success path
    const auto& result = cb.result();
    const auto& owned_room = result.room();
    // OwnedRoom { FfiOwnedHandle handle = 1; RoomInfo info = 2; }
    handle_ = FfiHandle(static_cast<uintptr_t>(owned_room.handle().id()));
    if (owned_room.info().has_sid()) {
        std::cout << "Room SID: " << owned_room.info().sid() << std::endl;
    }

    connected_ = true;
    std::cout << "Connected to room" << std::endl;
}

}
