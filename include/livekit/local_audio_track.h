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

/**
 * Represents a user-provided audio track sourced from the local device.
 *
 *  `LocalAudioTrack` is used to publish microphone audio (or any custom
 *  audio source) to a LiveKit room. It wraps a platform-specific audio
 *  source and exposes simple controls such as `mute()` and `unmute()`.
 *
 *  Typical usage:
 *
 *    auto source = AudioSource::create(...);
 *    auto track = LocalAudioTrack::createLocalAudioTrack("mic", source);
 *    room->localParticipant()->publishTrack(track);
 *
 *  Muting a local audio track stops transmitting audio to the room, but
 *  the underlying source may continue capturing depending on platform
 *  behavior.
 *
 *  The track name provided during creation is visible to remote
 *  participants and can be used for debugging or UI display.
 */
class LocalAudioTrack : public Track {
public:
  /// Creates a new local audio track backed by the given `AudioSource`.
  ///
  /// @param name   Human-readable name for the track. This may appear to
  ///               remote participants and in analytics/debug logs.
  /// @param source The audio source that produces PCM frames for this track.
  ///
  /// @return A shared pointer to the newly constructed `LocalAudioTrack`.
  static std::shared_ptr<LocalAudioTrack>
  createLocalAudioTrack(const std::string &name,
                        const std::shared_ptr<AudioSource> &source);

  /// Mutes the audio track.
  ///
  /// A muted track stops sending audio to the room, but the track remains
  /// published and can be unmuted later without renegotiation.
  void mute();

  /// Unmutes the audio track and resumes sending audio to the room.
  void unmute();

  /// Returns a human-readable string representation of the track,
  /// including its SID and name. Useful for debugging and logging.
  std::string to_string() const;

private:
  explicit LocalAudioTrack(FfiHandle handle, const proto::OwnedTrack &track);
};

} // namespace livekit