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
#error "Audio capture requires a capture-enabled LiveKit SDK; rebuild with --capture=gstreamer."
#endif

#include <cstdint>
#include <future>
#include <memory>
#include <string>

#include "livekit/audio_source.h"
#include "livekit/capture/capture_types.h"
#include "livekit/remote_audio_track.h"
#include "livekit/result.h"
#include "livekit/visibility.h"

namespace livekit {

class FfiClient;

namespace proto {
class OwnedAudioCaptureSource;
} // namespace proto

} // namespace livekit

namespace livekit::capture {

/// @brief Fixed PCM format produced by a GStreamer audio capture source.
struct AudioCaptureFormat {
  /// @brief Sample rate in Hz.
  std::uint32_t sample_rate = 0;

  /// @brief Number of interleaved channels.
  std::uint32_t num_channels = 0;

  /// @brief Number of samples per channel in each captured frame.
  std::uint32_t samples_per_channel = 0;
};

/// @brief Configuration for PCM S16LE audio ingest from GStreamer.
struct GstreamerAudioSourceConfig {
  /// @brief GStreamer launch description.
  ///
  /// @note Must contain `appsink name=lk_audio_appsink`.
  std::string pipeline;

  /// @brief Output sample rate in Hz.
  std::uint32_t sample_rate = 48000;

  /// @brief Number of interleaved output channels.
  std::uint32_t num_channels = 1;

  /// @brief Frame duration in milliseconds.
  std::uint32_t frame_duration_ms = 10;
};

/// @brief GStreamer audio capture source backed by a Rust capture pump.
///
/// Creation builds the pipeline asynchronously. The object owns both the
/// capture pump and the returned @ref AudioSource. Keep it alive while a local
/// audio track uses that source. This type is not safe for concurrent lifecycle
/// calls.
class LIVEKIT_API GstreamerAudioCaptureSource {
public:
  /// @brief Create a GStreamer audio capture source asynchronously.
  ///
  /// Completes asynchronously after Rust initializes the producer and RTC
  /// source. Keep the future to overlap setup with other work, or await it
  /// immediately. Destroying the future does not wait for creation; an
  /// abandoned result is released when creation completes.
  ///
  /// @param config Pipeline and fixed PCM output format.
  /// @return Future containing the created source on success.
  /// @throws std::runtime_error When awaiting the future if setup, transport,
  /// protocol validation, or SDK shutdown fails.
  /// @throws std::bad_alloc If allocation fails.
  [[nodiscard]] static std::future<Result<std::shared_ptr<GstreamerAudioCaptureSource>, CaptureError>> create(
      const GstreamerAudioSourceConfig& config);

  ~GstreamerAudioCaptureSource();

  GstreamerAudioCaptureSource(const GstreamerAudioCaptureSource&) = delete;
  GstreamerAudioCaptureSource& operator=(const GstreamerAudioCaptureSource&) = delete;
  GstreamerAudioCaptureSource(GstreamerAudioCaptureSource&&) = delete;
  GstreamerAudioCaptureSource& operator=(GstreamerAudioCaptureSource&&) = delete;

  /// @brief Negotiated fixed PCM format.
  /// @return Audio format reported by Rust.
  AudioCaptureFormat format() const noexcept;

  /// @brief RTC audio source fed by the capture pump.
  /// @return Owned audio source suitable for `LocalAudioTrack::createLocalAudioTrack()`.
  std::shared_ptr<AudioSource> audioSource() const noexcept;

  /// @brief Start pumping pipeline audio into the RTC source.
  /// @return Success when Rust acknowledges the request.
  /// @throws std::runtime_error If the request cannot be completed.
  [[nodiscard]] Result<void, CaptureError> start();

  /// @brief Signal capture to stop without waiting for cleanup.
  ///
  /// Call @ref join() to synchronize and retrieve the terminal result.
  /// Stopping an already-finished capture is a no-op.
  ///
  /// @return Success when Rust acknowledges the request.
  /// @throws std::runtime_error If the request cannot be completed.
  [[nodiscard]] Result<void, CaptureError> stop();

  /// @brief Wait asynchronously for capture to finish and return audio statistics.
  ///
  /// Call @ref stop() first for an unbounded pipeline. The returned
  /// @ref CaptureResult includes frame, sample, byte, and timestamp statistics.
  /// Join may be called only once after a successful @ref start().
  ///
  /// @return Future containing terminal statistics or a structured capture error.
  /// @throws std::runtime_error When awaiting the future if transport, protocol,
  /// cancellation, or SDK shutdown fails.
  [[nodiscard]] std::future<Result<CaptureResult, CaptureError>> join();

private:
  friend class livekit::FfiClient;

  GstreamerAudioCaptureSource();
  static std::shared_ptr<GstreamerAudioCaptureSource> fromOwned(const proto::OwnedAudioCaptureSource& owned);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

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
