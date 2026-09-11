/*
 * Copyright 2026 LiveKit
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
#include <optional>
#include <vector>

#include "livekit/room_event_types.h"
#include "livekit/video_source.h"
#include "livekit/visibility.h"

namespace livekit {

/// @brief Video source for publishing pre-encoded access units without re-encoding.
///
/// Capture calls are synchronous and copy the payload before returning. This
/// type is not safe for concurrent capture calls. Feedback polling can run on
/// another application thread.
class LIVEKIT_API EncodedVideoSource final : public VideoSource {
public:
  /// @brief One complete pre-encoded video access unit.
  struct Frame {
    /// True if this access unit is independently decodable.
    bool is_keyframe = false;
    /// Encoded frame width in pixels.
    /// Set both width and height to zero to use the source resolution.
    std::uint32_t width = 0;
    /// Encoded frame height in pixels.
    /// Set both width and height to zero to use the source resolution.
    std::uint32_t height = 0;
    /// Capture timestamp in microseconds. Set to zero to use the current time.
    std::int64_t timestamp_us = 0;
    /// Complete encoded access-unit payload.
    std::vector<std::uint8_t> data;
    /// Optional packet-trailer metadata.
    std::optional<VideoFrameMetadata> metadata;
  };

  /// @brief Latest encoder rate-control target requested by the publishing pipeline.
  struct RateControl {
    /// Requested target bitrate in bits per second.
    std::uint64_t target_bitrate_bps = 0;
    /// Requested frame rate in frames per second.
    double framerate_fps = 0.0;
  };

  /// @brief Pending feedback from the pre-encoded passthrough encoder.
  struct Feedback {
    /// True when the upstream encoder must produce a key frame.
    bool keyframe_requested = false;
    /// Latest rate-control target, if the publishing pipeline requested one.
    std::optional<RateControl> rate_control;
  };

  /// @brief Create a pre-encoded source for one codec and initial resolution.
  /// @param codec Codec carried by every frame submitted to this source. This
  ///              must match TrackPublishOptions::video_codec.
  /// @param width Initial source width in pixels. Must be in [1, 65535].
  /// @param height Initial source height in pixels. Must be in [1, 65535].
  /// @throws std::invalid_argument if either dimension is outside [1, 65535].
  /// @throws std::runtime_error if source creation fails.
  EncodedVideoSource(VideoCodec codec, int width, int height);

  /// @brief Codec carried by every frame submitted to this source.
  VideoCodec codec() const noexcept { return codec_; }

  /// @brief Submit one complete encoded access unit.
  /// @param frame Encoded frame. Its payload is copied during this call.
  /// @return True if the frame was accepted.
  /// @throws std::invalid_argument if the frame is empty, too large, has only
  ///         one zero dimension, or has a dimension above 65535.
  /// @throws std::runtime_error if the FFI request fails.
  [[nodiscard]] bool captureFrame(const Frame& frame) const;

  /// @brief Consume pending keyframe and rate-control feedback.
  /// @return Feedback accumulated since the previous call. Rate-control
  ///         updates use latest-value-wins semantics.
  /// @throws std::runtime_error if the FFI request fails.
  [[nodiscard]] Feedback takeFeedback() const;

private:
  using VideoSource::captureFrame;

  VideoCodec codec_;
};

} // namespace livekit
