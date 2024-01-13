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

#include "livekit/ffi_client.h"
#include "livekit/track.h"
#include "participant.pb.h"

namespace livekit {
class Room;

class Participant {
 public:
  Participant(const FfiHandle& handle, const proto::ParticipantInfo& info) : handle_(handle), info_(info) {}
  Participant(FfiHandle&& handle, proto::ParticipantInfo&& info) : handle_(std::move(handle)), info_(std::move(info)) {}

  const std::string& GetSid() const { return info_.sid(); }
  const std::string& GetIdentity() const { return info_.identity(); }
  const std::string& GetName() const { return info_.name(); }
  const std::string& GetMetadata() const { return info_.metadata(); }

 protected:
  FfiHandle handle_;
  proto::ParticipantInfo info_;
};

class LocalParticipant : public Participant {
 public:
  LocalParticipant(const FfiHandle& handle, const proto::ParticipantInfo& info)
      : Participant(handle, info) {}
  LocalParticipant(FfiHandle&& handle, proto::ParticipantInfo&& info)
      : Participant(std::move(handle), std::move(info)) {}

  ~LocalParticipant();

  void PublishTrack(std::shared_ptr<Track> track,
                    const proto::TrackPublishOptions& options);

 private:
  std::condition_variable cv_;  // Should we block?
  uint64_t publishAsyncId_;
  FfiClient::ListenerId listenerId_{0};
  std::unique_ptr<proto::PublishTrackCallback> publishCallback_;

  void OnEvent(const proto::FfiEvent& event);
};

}  // namespace livekit
#endif /* LIVEKIT_PARTICIPANT_H */
