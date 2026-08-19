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

#include <cstdint>

#include "livekit/track.h"
#include "livekit/track_publication.h"
#include "livekit/visibility.h"

namespace livekit {

namespace proto {
class OwnedTrackPublication;
}

/// @brief A track published by a remote participant.
class LIVEKIT_API RemoteTrackPublication : public TrackPublication {
public:
  /// Note, this RemoteTrackPublication is constructed internally only;
  /// safe to accept proto::OwnedTrackPublication.
  explicit RemoteTrackPublication(const proto::OwnedTrackPublication& owned);

  /// @brief Returns the locally recorded subscription state.
  ///
  /// @return True if the publication is marked as subscribed.
  bool subscribed() const noexcept { return subscribed_; }

  /// @brief Changes the desired subscription state for this publication.
  ///
  /// @param subscribed True to subscribe; false to unsubscribe.
  /// @throws std::runtime_error if the publication has an invalid FFI handle or
  /// the FFI request fails.
  void setSubscribed(bool subscribed);

  /// @brief Returns whether media delivery is enabled for this publication.
  ///
  /// @return True if media delivery is enabled.
  bool enabled() const noexcept { return enabled_; }

  /// @brief Enables or disables media delivery for this publication.
  ///
  /// Disabling a subscribed publication reduces bandwidth without changing its
  /// desired subscription state.
  ///
  /// @param enabled True to enable media delivery; false to disable it.
  /// @return True if an update was sent; false if the state is unchanged or
  /// the publication is not subscribed.
  /// @throws std::runtime_error if the publication has an invalid FFI handle or
  /// the FFI request fails.
  bool setEnabled(bool enabled);

  /// @brief Returns the requested maximum simulcast quality.
  ///
  /// @return The requested quality, or @c VideoQuality::HIGH when dimensions
  /// are preferred.
  VideoQuality videoQuality() const noexcept { return video_quality_; }

  /// @brief Requests the maximum simulcast quality to receive.
  ///
  /// This overrides a previous call to setVideoDimensions().
  ///
  /// @param quality Maximum acceptable simulcast quality.
  /// @return True if an update was sent; false if the quality is unchanged or
  /// the publication is not a subscribed, simulcasted video track.
  /// @throws std::runtime_error if the publication has an invalid FFI handle or
  /// the FFI request fails.
  bool setVideoQuality(VideoQuality quality);

  /// @brief Requests the maximum video dimensions to receive for this publication.
  ///
  /// The server selects the closest available simulcast or scalable-video layer.
  /// This overrides a previous call to setVideoQuality().
  /// Repeating the current dimensions does not send another request.
  ///
  /// @param width Requested maximum width in pixels. Must be greater than zero.
  /// @param height Requested maximum height in pixels. Must be greater than zero.
  /// @return True if an update was sent; false if the dimensions are unchanged
  /// or the publication is not a subscribed video track.
  /// @throws std::runtime_error if the publication has an invalid FFI handle or
  /// the FFI request fails.
  bool setVideoDimensions(std::uint32_t width, std::uint32_t height);

private:
  enum class VideoPreference {
    DEFAULT,
    DIMENSIONS,
    QUALITY,
  };

  bool subscribed_{false};
  bool enabled_{true};
  VideoPreference video_preference_{VideoPreference::DEFAULT};
  VideoQuality video_quality_{VideoQuality::HIGH};
};

} // namespace livekit
