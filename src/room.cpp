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

    FFIRequest request;
    request.set_allocated_connect(connectRequest);
    
    // TODO Free:
    FfiClient::getInstance().AddListener(std::bind(&Room::OnEvent, this, std::placeholders::_1));

    FFIResponse response = FfiClient::getInstance().SendRequest(request);
    FFIAsyncId asyncId = response.connect().async_id();

    connectAsyncId_ = asyncId.id();
}

void Room::OnEvent(const FFIEvent& event)
{
    std::lock_guard<std::mutex> guard(lock_);
    if (!connected_) {
        return;
    }
    
    if (event.has_connect()) {
        ConnectCallback connectCallback = event.connect();
        if (connectCallback.async_id().id() != connectAsyncId_) {
            return;
        }

        std::cout << "Received ConnectCallback" << std::endl;

        if (!connectCallback.has_error()) {
            handle_ = FfiHandle(connectCallback.room().handle().id());

            std::cout << "Connected to room" << std::endl;
            std::cout << "Room SID: " << connectCallback.room().sid() << std::endl;
        } else {
            std::cerr << "Failed to connect to room: " << connectCallback.error() << std::endl;
        }
    }
}

}
