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

void Room::Connect(const std::string& url, const std::string& token)
{
   std::lock_guard<std::mutex> guard(lock_);
    if (connected_) {
        throw std::runtime_error("already connected");
    }

    connected_ = true;

    RoomOptions *options = new RoomOptions;
    options->set_auto_subscribe(true);
    
    ConnectRequest *connectRequest = new ConnectRequest;
    connectRequest->set_url(url);
    connectRequest->set_token(token);
    connectRequest->set_allocated_options(options);

    proto::FfiRequest request;
    request.set_allocated_connect(connectRequest);
    
    // TODO Free:
    FfiClient::getInstance().AddListener(std::bind(&Room::OnEvent, this, std::placeholders::_1));
    proto::FfiResponse response = FfiClient::getInstance().SendRequest(request);
    connectAsyncId_ = response.connect().async_id();
}

void Room::OnEvent(const FfiEvent& event) {
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
