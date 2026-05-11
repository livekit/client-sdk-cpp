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

#include "local_track_publication.h"
#include "track.h"

namespace livekit {

namespace proto {
class OwnedTrack;
}

class VideoSource;

/**
 * Represents a user-provided video track sourced from the local device.
 *
 *  `LocalVideoTrack` is used to publish camera video (or any custom
 *  video source) to a LiveKit room. It wraps a platform-specific video
 *  source and exposes simple controls such as `mute()` and `unmute()`.
 *
 *  Typical usage:
 *
 *    auto source = std::make_shared<VideoSource>(1280, 720);
 *    auto track = LocalVideoTrack::createLocalVideoTrack("cam", source);
 *    room->localParticipant()->publishTrack(track);
 *    // Capture frames on the video thread via `source`, not via the track.
 *
 *  Muting a local video track stops transmitting video to the room, but
 *  the underlying source may continue capturing depending on platform
 *  behavior.
 *
 *  The track name provided during creation is visible to remote
 *  participants and can be used for debugging or UI display.
 */
class LocalVideoTrack : public Track {
public:
  /// Creates a new local video track backed by the given `VideoSource`.
  ///
  /// @param name   Human-readable name for the track. This may appear to
  ///               remote participants and in analytics/debug logs.
  /// @param source The video source that produces video frames for this track.
  ///               The caller retains ownership and should use this source
  ///               directly for frame capture.
  ///
  /// @return A shared pointer to the newly constructed `LocalVideoTrack`.
  static std::shared_ptr<LocalVideoTrack> createLocalVideoTrack(const std::string& name,
                                                                const std::shared_ptr<VideoSource>& source);

  /// Mutes the video track.
  ///
  /// A muted track stops sending video to the room, but the track remains
  /// published and can be unmuted later without renegotiation.
  void mute();

  /// Unmutes the video track and resumes sending video to the room.
  void unmute();

  /// Returns a human-readable string representation of the track,
  /// including its SID and name. Useful for debugging and logging.
  std::string to_string() const;

  /// Returns the publication that owns this track, or nullptr if the track is
  /// not published.
  std::shared_ptr<LocalTrackPublication> publication() const noexcept { return local_publication_; }

  /// Sets the publication that owns this track.
  /// Note: std::move on a const& silently falls back to a copy, so we assign
  /// directly. Changing the virtual signature to take by value would enable
  /// a true move but is a API-breaking change hence left for a future revision.
  void setPublication(const std::shared_ptr<LocalTrackPublication>& publication) noexcept override {
    local_publication_ = publication;
  }

private:
  explicit LocalVideoTrack(FfiHandle handle, const proto::OwnedTrack& track);

  /// The publication that owns this track. This is a nullptr until the track
  /// is published, and then points to the publication that owns this track.
  std::shared_ptr<LocalTrackPublication> local_publication_;
};

} // namespace livekit