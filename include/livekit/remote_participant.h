/*
 * Copyright 2025 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
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

#pragma once

#include "participant.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace livekit {

class RemoteTrackPublication;

class RemoteParticipant : public Participant {
public:
  using PublicationMap =
      std::unordered_map<std::string, std::shared_ptr<RemoteTrackPublication>>;

  RemoteParticipant(FfiHandle handle, std::string sid, std::string name,
                    std::string identity, std::string metadata,
                    std::unordered_map<std::string, std::string> attributes,
                    ParticipantKind kind, DisconnectReason reason);

  // A dictionary of track publications associated with the participant.
  const PublicationMap &trackPublications() const noexcept {
    return track_publications_;
  }

  // Optional: non-const access if you want to mutate in-place.
  PublicationMap &mutableTrackPublications() noexcept {
    return track_publications_;
  }

  std::string to_string() const;

protected:
  // Called by Room events like kTrackMuted. This is internal plumbing and not
  // intended to be called directly by SDK users.
  std::shared_ptr<TrackPublication>
  findTrackPublication(const std::string &sid) const override;
  friend class Room;

private:
  PublicationMap track_publications_;
};

// Convenience for logging / streaming
std::ostream &operator<<(std::ostream &os,
                         const RemoteParticipant &participant);

} // namespace livekit
