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

#include "livekit/remote_participant.h"

#include <ostream>
#include <sstream>
#include <utility>

namespace livekit {

RemoteParticipant::RemoteParticipant(
    FfiHandle handle, std::string sid, std::string name, std::string identity,
    std::string metadata,
    std::unordered_map<std::string, std::string> attributes,
    ParticipantKind kind, DisconnectReason reason)
    : Participant(std::move(handle), std::move(sid), std::move(name),
                  std::move(identity), std::move(metadata),
                  std::move(attributes), kind, reason),
      track_publications_() {}

std::string RemoteParticipant::to_string() const {
  std::ostringstream oss;
  oss << "rtc.RemoteParticipant(sid=" << sid() << ", identity=" << identity()
      << ", name=" << name() << ")";
  return oss.str();
}

std::ostream &operator<<(std::ostream &os,
                         const RemoteParticipant &participant) {
  os << participant.to_string();
  return os;
}

} // namespace livekit
