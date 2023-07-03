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

#ifndef LIVEKIT_PARTICIPANT_H
#define LIVEKIT_PARTICIPANT_H

#include <condition_variable>
#include <memory>

#include "livekit/room.h"
#include "livekit/track.h"

#include "participant.pb.h"
#include "ffi.pb.h"
#include "room.pb.h"

namespace livekit {

    class Participant {
    public:
        Participant(const ParticipantInfo& info, std::weak_ptr<Room> room) : info_(info), room_(room) {}

        const std::string& GetSid() const { return info_.sid(); }
        const std::string& GetIdentity() const { return info_.identity(); }
        const std::string& GetName() const { return info_.name(); }
        const std::string& GetMetadata() const { return info_.metadata(); }

    protected:
        ParticipantInfo info_;
        std::weak_ptr<Room> room_;
    };

    class LocalParticipant : public Participant {
    public:
        LocalParticipant(const ParticipantInfo& info, std::weak_ptr<Room> room) : Participant(info, room) {}

        void PublishTrack(std::shared_ptr<Track> track, const TrackPublishOptions& options);

    private:
        mutable std::mutex lock_;
        std::condition_variable cv_;
        FfiAsyncId publishAsyncId_;
        FfiClient::ListenerId listenerId_;
        std::unique_ptr<PublishTrackCallback> publishCallback_;

        void OnEvent(const FfiEvent& event);
    };

}
#endif /* LIVEKIT_PARTICIPANT_H */
