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

#include <memory>
#include <string>

#include "livekit/audio_frame.h"
#include "livekit/local_track_publication.h"
#include "livekit/track.h"
#include "livekit/visibility.h"

namespace livekit {

namespace proto {
class OwnedTrack;
}

class AudioSource;
class PlatformAudioSource;

/// Represents a user-provided audio track sourced from the local device.
///
/// `LocalAudioTrack` is used to publish microphone audio or any custom audio
/// source to a LiveKit room. It wraps a platform-specific audio
/// source and exposes simple controls such as `mute()` and `unmute()`.
/// Muting a local audio track stops transmitting audio to the room, but
/// the underlying source may continue capturing depending on platform behavior.
///
///  Typical usage:
///
///    auto source = AudioSource::create(...);
///    auto track = LocalAudioTrack::createLocalAudioTrack("mic", source);
///    if (auto lp = room->localParticipant().lock()) {
///      lp->publishTrack(track);
///    }
///
///  Muting a local audio track stops transmitting audio to the room, but
///  the underlying source may continue capturing depending on platform
///  behavior.
///
///  The track name provided during creation is visible to remote
///  participants and can be used for debugging or UI display.
/// @note This operation is not thread-safe.
class LIVEKIT_API LocalAudioTrack : public Track {
public:
  /// Creates a new local audio track backed by the given `AudioSource`.
  ///
  /// @param name   Human-readable name for the track. This may appear to
  ///               remote participants and in analytics/debug logs.
  /// @param source The audio source that produces PCM frames for this track.
  ///               The caller retains ownership and should use this source
  ///               directly for frame capture.
  ///
  /// @return A shared pointer to the newly constructed `LocalAudioTrack`.
  /// @throws std::invalid_argument  If \p source is null.
  /// @throws std::runtime_error If the FFI request fails.
  static std::shared_ptr<LocalAudioTrack> createLocalAudioTrack(const std::string& name,
                                                                const std::shared_ptr<AudioSource>& source);

  /// Creates a new local audio track backed by the given `PlatformAudioSource`.
  ///
  /// @param name   Human-readable name for the track. This may appear to
  ///               remote participants and in analytics/debug logs.
  /// @param source  The platform source that captures microphone audio
  ///               automatically through WebRTC's Audio Device Module.
  ///
  /// @return A shared pointer to the newly constructed `LocalAudioTrack`.
  /// @throws std::invalid_argument  If \p source is null.
  /// @throws std::runtime_error If the FFI request fails.
  ///
  /// @note This operation is not thread-safe.
  static std::shared_ptr<LocalAudioTrack> createLocalAudioTrack(const std::string& name,
                                                                const std::shared_ptr<PlatformAudioSource>& source);

  /// Mutes the audio track.
  ///
  /// A muted track stops sending audio to the room, but the track remains
  /// published and can be unmuted later without renegotiation.
  ///
  /// @throws std::runtime_error If the FFI request fails.
  void mute();

  /// Unmute the audio track.
  ///
  /// Resumes sending audio to the room.
  ///
  /// @throws std::runtime_error If the FFI request fails.
  void unmute();

  /// Return a human-readable string representation of the track.
  ///
  /// @return String containing the track SID and name.
  std::string toString() const;

  /// Returns the publication that owns this track, or nullptr if the track is
  /// not published.
  std::shared_ptr<LocalTrackPublication> publication() const noexcept { return local_publication_; }

  /// Set the publication that owns this track.
  ///
  /// @param publication  Publication that owns this track, or nullptr to clear
  /// the association.
  void setPublication(const std::shared_ptr<LocalTrackPublication>& publication) noexcept override {
    local_publication_ = publication;
  }

private:
  explicit LocalAudioTrack(FfiHandle handle, const proto::OwnedTrack& track);

  /// The publication that owns this track. This is a nullptr until the track
  /// is published, and then points to the publication that owns this track.
  std::shared_ptr<LocalTrackPublication> local_publication_;
};

} // namespace livekit
