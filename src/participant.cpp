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

#include "livekit/participant.h"
#include "livekit/ffi_client.h"

#include "ffi.pb.h"

namespace livekit
{
    void LocalParticipant::PublishTrack(std::shared_ptr<Track> track, const TrackPublishOptions& options) {
        // TODO: Add audio track support
        if (!dynamic_cast<LocalVideoTrack*>(track.get())) {
            throw std::runtime_error("cannot publish a remote track");
        }

        std::shared_ptr<Room> room = room_.lock();
        if (room == nullptr) {
            throw std::runtime_error("room is closed");
        }

        FFIRequest request;
        PublishTrackRequest* publishTrackRequest = request.mutable_publish_track();
        publishTrackRequest->mutable_track_handle()->set_id(track->GetHandle().handle);
        publishTrackRequest->mutable_room_handle()->set_id(room->GetHandle().handle);
        *publishTrackRequest->mutable_options() = options;

        PublishTrackResponse resp = FfiClient::getInstance().SendRequest(request).publish_track();
        publishAsyncId_ = resp.async_id();

        listenerId_ = FfiClient::getInstance().AddListener(std::bind(&LocalParticipant::OnEvent, this, std::placeholders::_1));
        std::unique_lock lock(lock_);
        
        cv_.wait(lock, [this]{ return publishCallback_ != nullptr; });
        // TODO: Handle errors
    }

    void LocalParticipant::OnEvent(const FFIEvent& event) {
        if (event.has_publish_track()) {
            PublishTrackCallback cb = event.publish_track();
            if (cb.async_id().id() == publishAsyncId_.id()) {
                publishCallback_ = std::make_unique<PublishTrackCallback>(cb);
                FfiClient::getInstance().RemoveListener(listenerId_);
            }
        }
    }
}
