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

#include "livekit/track_publication.h"

namespace livekit {

namespace proto {
class OwnedTrackPublication;
}

class Track;

class LocalTrackPublication : public TrackPublication {
public:
  /// Note, this RemoteTrackPublication is constructed internally only;
  /// safe to accept proto::OwnedTrackPublication.
  explicit LocalTrackPublication(const proto::OwnedTrackPublication &owned);

  /// Typed accessor for the attached LocalTrack (if any).
  std::shared_ptr<Track> track() const noexcept;
};

} // namespace livekit
