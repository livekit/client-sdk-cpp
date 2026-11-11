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

#include "livekit/build.h"

#if !LIVEKIT_CAPTURE_ENABLED && !defined(LIVEKIT_BUILDING_SDK)
#error "Audio playout requires a capture-enabled LiveKit SDK; rebuild with --capture=gstreamer."
#endif

#include <cstdint>
#include <future>
#include <memory>
#include <string>

#include "livekit/remote_audio_track.h"
#include "livekit/visibility.h"

namespace livekit {
class FfiClient;
} // namespace livekit

namespace livekit::capture {

/// @brief Configuration for mixed PCM S16LE playout through GStreamer.
struct GstreamerAudioPlayoutConfig {
  /// @brief GStreamer launch description.
  ///
  /// The pipeline must contain `appsrc name=lk_audio_appsrc`.
  std::string pipeline;

  /// @brief Mixed output sample rate in Hz.
  std::uint32_t sample_rate = 48000;

  /// @brief Number of interleaved output channels.
  std::uint32_t num_channels = 1;

  /// @brief Mixed output frame duration in milliseconds.
  std::uint32_t frame_duration_ms = 10;
};

/// @brief Terminal statistics for GStreamer audio playout.
struct AudioPlayoutStats {
  /// @brief Number of frames accepted by the GStreamer pipeline.
  std::uint64_t frames_pushed = 0;

  /// @brief Number of frames dropped because the pipeline could not accept them.
  std::uint64_t frames_dropped = 0;

  /// @brief Number of PCM bytes accepted by the pipeline.
  std::uint64_t bytes_pushed = 0;
};

/// @brief GStreamer sink that dynamically mixes remote audio tracks.
///
/// The sink starts with no tracks. Attach and detach operations complete
/// asynchronously on the Rust runtime. Calling @ref stop() consumes the
/// server-side playout and returns its terminal statistics. This type is safe
/// for concurrent attach and detach calls; stop must be externally serialized
/// with those operations.
class LIVEKIT_API GstreamerAudioPlayoutSink {
public:
  /// @brief Create and start an empty GStreamer audio playout pipeline.
  ///
  /// @param config Pipeline and fixed mixed PCM format.
  /// @return The running playout sink.
  /// @throws std::runtime_error If the SDK is uninitialized, the pipeline is
  /// invalid, or Rust returns a malformed response.
  /// @throws std::bad_alloc If allocation fails.
  [[nodiscard]] static std::shared_ptr<GstreamerAudioPlayoutSink> create(const GstreamerAudioPlayoutConfig& config);

  ~GstreamerAudioPlayoutSink();

  GstreamerAudioPlayoutSink(const GstreamerAudioPlayoutSink&) = delete;
  GstreamerAudioPlayoutSink& operator=(const GstreamerAudioPlayoutSink&) = delete;
  GstreamerAudioPlayoutSink(GstreamerAudioPlayoutSink&&) = delete;
  GstreamerAudioPlayoutSink& operator=(GstreamerAudioPlayoutSink&&) = delete;

  /// @brief Attach a remote audio track to the mixer.
  /// @param track Remote audio track whose handle Rust will clone.
  /// @return Future containing the stable track ID used by @ref detachTrack().
  /// @throws std::runtime_error When awaiting the future if the operation fails.
  [[nodiscard]] std::future<std::string> attachTrack(const RemoteAudioTrack& track);

  /// @brief Detach an attached track by its stable ID.
  /// @param track_id ID returned by @ref attachTrack().
  /// @return Future containing true if the track was attached and removed.
  /// @throws std::runtime_error When awaiting the future if the operation fails.
  [[nodiscard]] std::future<bool> detachTrack(const std::string& track_id);

  /// @brief Stop playout asynchronously and return terminal statistics.
  ///
  /// This operation may be called once. Destruction also stops playout, but
  /// discards terminal statistics.
  ///
  /// @return Future containing playout statistics.
  /// @throws std::runtime_error When awaiting the future if the operation fails.
  [[nodiscard]] std::future<AudioPlayoutStats> stop();

private:
  friend class livekit::FfiClient;

  GstreamerAudioPlayoutSink();
  static std::shared_ptr<GstreamerAudioPlayoutSink> fromHandle(std::uint64_t handle);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace livekit::capture
