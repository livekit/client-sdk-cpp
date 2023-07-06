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
    Room::Room() {
        eventListenerId_ = FfiClient::getInstance().AddListener(std::bind(&Room::OnEvent, this, std::placeholders::_1));
    }

    Room::~Room() {
        FfiClient::getInstance().RemoveListener(eventListenerId_);
    }

    void Room::Connect(const std::string& url, const std::string& token)
    {
        // RoomOptions *options = new RoomOptions;
        // options->set_auto_subscribe(true);
        
        FfiRequest request;
        ConnectRequest *connectRequest = request.mutable_connect();
        connectRequest->set_url(url);
        connectRequest->set_token(token);
        // connectRequest->set_allocated_options(options);

        FfiResponse response = FfiClient::getInstance().SendRequest(request);
        connectAsyncId_ = response.connect().async_id().id();
    }

    // void Room::PublishVideoTrack(const std::string& name, const std::string& sid, const std::string& inputTrackSid)
    // {
    //     std::lock_guard<std::mutex> guard(lock_);
    //     if (!connected_) {
    //         throw std::runtime_error("not connected");
    //     }

    //     PublishTrackRequest *publishTrackRequest = new PublishTrackRequest;
    //     publishTrackRequest->set_kind(TrackType::VIDEO);
    //     publishTrackRequest->set_name(name);
    //     publishTrackRequest->set_sid(sid);
    //     publishTrackRequest->set_input_track_sid(inputTrackSid);

    //     FFIRequest request;
    //     request.set_allocated_publish_track(publishTrackRequest);

    //     FfiResponse response = FfiClient::getInstance().SendRequest(request);
    //     FFIAsyncId asyncId = response.publish_track().async_id();

    //     std::cout << "Publishing video track" << std::endl;
    // }

    void Room::OnEvent(const FfiEvent& event)
    {
        if (event.has_connect()) {
            ConnectCallback connectCallback = event.connect();
            if (connectCallback.async_id().id() != connectAsyncId_) {
                return;
            }

            std::cout << "Received ConnectCallback" << std::endl;

            if (!connectCallback.has_error()) {
                handle_ = FfiHandle(connectCallback.room().handle().id());
                roomInfo_ = connectCallback.room();
                localParticipant_ = std::make_shared<LocalParticipant>(connectCallback.room().local_participant(), shared_from_this());

                std::cout << "Connected to room" << std::endl;
                std::cout << "Room SID: " << connectCallback.room().sid() << std::endl;
            } else {
                std::cerr << "Failed to connect to room: " << connectCallback.error() << std::endl;
            }
        }
    }

}
