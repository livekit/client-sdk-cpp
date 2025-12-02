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

#include "track.h"
#include <memory>
#include <string>

namespace livekit {

namespace proto {
class OwnedTrack;
}

class VideoSource;

/**
 * Represents an video track published by a remote participant and
 * subscribed to by the local participant.
 *
 * `RemoteVideoTrack` instances are created internally when the SDK receives a
 * `kTrackSubscribed` event. Each instance is owned by its associated
 * `RemoteParticipant` and delivered to the application via
 * `TrackSubscribedEvent`.
 *
 * Applications generally interact with `RemoteVideoTrack` through events and
 * `RemoteTrackPublication`, not through direct construction.
 */
class RemoteVideoTrack : public Track {
public:
  /// Constructs a `RemoteVideoTrack` from an internal protocol-level
  /// `OwnedTrack` description provided by the signaling/FFI layer.
  /// This constructor is intended for internal SDK use only.
  explicit RemoteVideoTrack(const proto::OwnedTrack &track);

  /// Returns a concise, human-readable string summarizing the track,
  /// including its SID and name. Useful for debugging and logging.
  std::string to_string() const;

private:
};

} // namespace livekit