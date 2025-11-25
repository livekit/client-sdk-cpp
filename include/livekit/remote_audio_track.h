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

class AudioSource;

// ============================================================
// RemoteAudioTrack
// ============================================================
class RemoteAudioTrack : public Track {
public:
  explicit RemoteAudioTrack(FfiHandle handle, const proto::OwnedTrack &track);

  static std::shared_ptr<RemoteAudioTrack>
  createRemoteAudioTrack(const std::string &name,
                         const std::shared_ptr<AudioSource> &source);

  std::string to_string() const;

private:
};

} // namespace livekit