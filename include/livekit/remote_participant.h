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
  using TrackPublicationMap =
      std::unordered_map<std::string, std::shared_ptr<RemoteTrackPublication>>;

  RemoteParticipant(FfiHandle handle, std::string sid, std::string name,
                    std::string identity, std::string metadata,
                    std::unordered_map<std::string, std::string> attributes,
                    ParticipantKind kind, DisconnectReason reason);

  // A dictionary of track publications associated with the participant.
  const TrackPublicationMap &track_publications() const noexcept {
    return track_publications_;
  }

  // Optional: non-const access if you want to mutate in-place.
  TrackPublicationMap &mutable_track_publications() noexcept {
    return track_publications_;
  }

  // C++ equivalent of Python's __repr__
  std::string to_string() const;

private:
  TrackPublicationMap track_publications_;
};

// Convenience for logging / streaming
std::ostream &operator<<(std::ostream &os,
                         const RemoteParticipant &participant);

} // namespace livekit
