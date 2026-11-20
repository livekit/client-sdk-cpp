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
#include "livekit/result.h"
#include "livekit/visibility.h"

namespace livekit {

class FfiClient;

namespace proto {
class OwnedAudioCaptureSource;
} // namespace proto

} // namespace livekit

namespace livekit::capture {

/// @brief Fixed PCM format produced in 10 ms frames by an audio capture source.
struct AudioCaptureFormat {
  /// @brief Sample rate in Hz.
  std::uint32_t sample_rate = 0;

  /// @brief Number of interleaved channels.
  std::uint32_t num_channels = 0;

  /// @brief Number of samples per channel in each captured frame.
  std::uint32_t samples_per_channel = 0;
};

/// @brief Configuration for PCM S16LE audio ingest in 10 ms frames from GStreamer.
struct GstreamerAudioSourceConfig {
  /// @brief GStreamer launch description.
  ///
  /// @note Must contain `appsink name=lk_audio_appsink`.
  std::string pipeline;

  /// @brief Output sample rate in Hz.
  std::uint32_t sample_rate = 48000;

  /// @brief Number of interleaved output channels.
  std::uint32_t num_channels = 1;
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
  /// @ref CaptureResult includes the number of accepted 10 ms frames and exit reason.
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

} // namespace livekit::capture
