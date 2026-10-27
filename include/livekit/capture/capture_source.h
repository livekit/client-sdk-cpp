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

// Internal SDK wrappers are built for both variants; consumers require capture.
#if !LIVEKIT_CAPTURE_ENABLED && !defined(LIVEKIT_BUILDING_SDK)
#error "Capture sources require a capture-enabled LiveKit SDK; rebuild with --capture."
#endif

#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>

#include "livekit/result.h"
#include "livekit/room_event_types.h"
#include "livekit/video_source.h"
#include "livekit/visibility.h"

namespace livekit {

class FfiClient;

namespace proto {
class OwnedCaptureSource;
} // namespace proto

} // namespace livekit

/// @brief Capture sources and their configuration types.
namespace livekit::capture {

/// @brief Kind of media a capture source produces.
enum class CaptureSourceKind {
  /// @brief Pixel frames, published through the WebRTC encoder.
  Pixel = 0,
  /// @brief Pre-encoded access units, published as passthrough.
  Encoded = 1,
};

/// @brief Bitrate unit expected by a GStreamer encoder property.
enum class GstreamerBitrateUnit {
  /// @brief Bits per second.
  Bps = 0,
  /// @brief Kilobits per second.
  Kbps = 1,
};

/// @brief Video resolution in pixels.
struct CaptureResolution {
  int width = 0;
  int height = 0;
};

/// @brief Binding from WebRTC rate-control targets to a GStreamer encoder property.
struct GstreamerRateControl {
  /// @brief Name of the encoder element in the pipeline (e.g. `lk_encoder`).
  std::string element;

  /// @brief Bitrate property to set on the element (e.g. `bitrate` for x264enc,
  /// `target-bitrate` for vp8enc/vp9enc).
  std::string property;

  /// @brief Unit the property expects.
  GstreamerBitrateUnit unit = GstreamerBitrateUnit::Bps;
};

/// @brief Configuration for encoded ingest from a GStreamer pipeline.
struct GstreamerVideoSourceConfig {
  /// @brief GStreamer launch description for the encoded producer pipeline.
  ///
  /// @note Must contain `appsink name=lk_appsink`, or leave exactly one encoded
  /// video source pad unlinked for the source to attach one to.
  std::string pipeline;

  /// @brief Codec expected from the pipeline; inferred from pipeline caps when
  /// omitted.
  std::optional<VideoCodec> codec;

  /// @brief Encoded frame resolution. When omitted, it is discovered from the
  /// pipeline's negotiated caps; when set, the pipeline output is verified
  /// against it.
  std::optional<CaptureResolution> resolution;

  /// @brief Forwards WebRTC rate-control targets to an encoder element's bitrate
  /// property. Without this, the pipeline encodes at a fixed bitrate.
  std::optional<GstreamerRateControl> rate_control;
};

/// @brief Test pattern rendered by the built-in pattern source.
enum class Pattern {
  /// @brief Animated color gradient.
  Gradient = 0,
  /// @brief Bouncing LiveKit logo.
  Logo = 1,
};

/// Configuration for the built-in test source rendering a @ref Pattern.
struct PatternVideoSourceConfig {
  /// @brief Output resolution; both components must be positive.
  CaptureResolution resolution;

  /// @brief Output frame rate in frames per second; must be non-zero.
  std::uint32_t framerate_fps = 0;

  /// @brief Pattern to render.
  Pattern pattern = Pattern::Gradient;
};

/// @brief Wall clock with millisecond precision, rendered on the GPU.
/// Shows the local time of the machine running the FFI server.
struct ClockVideoSourceConfig {
  /// @brief Output resolution; both components must be positive.
  CaptureResolution resolution;

  /// @brief Output frame rate in frames per second; must be non-zero.
  std::uint32_t framerate_fps = 0;
};

/// @brief Why a capture ended without error.
enum class CaptureExit {
  /// Stopped via @ref CaptureSource::stop (or source destruction).
  Stopped = 0,
  /// @brief The producer reached the end of its stream.
  EndOfStream = 1,
};

/// @brief Terminal pump failure category; matches the FFI CaptureErrorKind enum.
enum class CaptureErrorKind {
  /// @brief The capture source failed or violated its contract.
  Source = 0,
  /// @brief The RTC video source rejected a frame.
  Rtc = 1,
  /// @brief The pump thread panicked.
  Panicked = 2,
};

/// @brief Structured terminal pump failure reported by the Rust FFI.
struct CaptureError {
  /// @brief Human-readable error message.
  std::string message;

  /// @brief Error category.
  CaptureErrorKind kind = CaptureErrorKind::Source;
};

/// @brief Successful terminal statistics of a started capture.
struct CaptureResult {
  /// @brief Number of frames or access units captured.
  std::uint64_t frames_captured = 0;

  /// @brief Why the capture ended successfully.
  CaptureExit exit = CaptureExit::Stopped;
};

/// @brief A server-side capture source (livekit-capture) that produces video
/// without per-frame FFI traffic.
///
/// The source owns its producer (e.g. a GStreamer pipeline) and the pump
/// that feeds an RTC video source. Publish it like any other source: create
/// a track from @ref CaptureSource::videoSource(), merge application options
/// through @ref CaptureSource::publishOptions(), then call @ref CaptureSource::start().
///
/// Structured pump failures are returned as Result values containing @ref CaptureError.
/// Failures without a protobuf error kind throw exceptions. Invalid C++ input
/// throws std::invalid_argument; initialization, transport, protocol, cancellation,
/// and untyped Rust failures throw std::runtime_error. For create() and join(),
/// these exceptions are delivered when awaiting the future.
///
/// Requires a capture-enabled SDK build (`LIVEKIT_CAPTURE`). Creation fails
/// when the requested source was not compiled into the Rust FFI.
///
/// @note Keep this object alive while the track is published. Destroying it
/// stops a running capture.
class LIVEKIT_API CaptureSource {
public:
  /// @brief Create a capture source from a GStreamer pipeline configuration.
  ///
  /// Completes asynchronously: construction starts the pipeline and may wait
  /// for its first output to discover stream settings. Configuration, producer
  /// setup, transport, protocol, and cancellation failures are delivered as
  /// exceptions through the future.
  /// Destroying the future does not wait for creation. The source is released
  /// when creation completes if the future was discarded.
  ///
  /// @param config GStreamer pipeline configuration.
  /// @return A future containing a successful Result with the created source.
  /// @throws std::invalid_argument When awaiting the future if the C++ configuration is invalid.
  /// @throws std::runtime_error When awaiting the future if setup, initialization,
  /// transport, protocol validation, or cancellation fails.
  /// @throws std::bad_alloc If allocation fails, including when awaiting the future.
  [[nodiscard]] static std::future<Result<std::shared_ptr<CaptureSource>, CaptureError>> create(
      const GstreamerVideoSourceConfig& config);

  /// @brief Create the built-in test pattern capture source.
  ///
  /// Completes asynchronously after Rust initializes the GPU renderer.
  /// Keep the future to overlap setup with other work, or await it immediately.
  /// Destroying the future does not wait for creation; an abandoned result is
  /// released when creation completes.
  ///
  /// @param config Pattern source configuration.
  /// @return A future containing a successful Result with the created source.
  /// @throws std::invalid_argument When awaiting the future if the C++ configuration is invalid.
  /// @throws std::runtime_error When awaiting the future if setup, initialization,
  /// transport, protocol validation, or cancellation fails.
  /// @throws std::bad_alloc If allocation fails, including when awaiting the future.
  [[nodiscard]] static std::future<Result<std::shared_ptr<CaptureSource>, CaptureError>> create(
      PatternVideoSourceConfig config);

  /// @brief Create the built-in wall clock capture source.
  ///
  /// Completes asynchronously after Rust initializes the GPU renderer.
  /// Keep the future to overlap setup with other work, or await it immediately.
  /// Destroying the future does not wait for creation; an abandoned result is
  /// released when creation completes.
  ///
  /// @param config Clock source configuration.
  /// @return A future containing a successful Result with the created source.
  /// @throws std::invalid_argument When awaiting the future if the C++ configuration is invalid.
  /// @throws std::runtime_error When awaiting the future if setup, initialization,
  /// transport, protocol validation, or cancellation fails.
  /// @throws std::bad_alloc If allocation fails, including when awaiting the future.
  [[nodiscard]] static std::future<Result<std::shared_ptr<CaptureSource>, CaptureError>> create(
      ClockVideoSourceConfig config);

  ~CaptureSource();

  CaptureSource(const CaptureSource&) = delete;
  CaptureSource& operator=(const CaptureSource&) = delete;
  CaptureSource(CaptureSource&&) = delete;
  CaptureSource& operator=(CaptureSource&&) = delete;

  /// @brief Kind of media this source produces.
  /// @return The source kind.
  CaptureSourceKind kind() const noexcept;

  /// @brief Declared or discovered stream resolution.
  /// @return Width in pixels.
  int width() const noexcept;

  /// @brief Declared or discovered stream height.
  /// @return Height in pixels.
  int height() const noexcept;

  /// @brief Codec produced by the source; encoded sources only.
  /// @return The codec, or no value for a pixel source.
  std::optional<VideoCodec> codec() const noexcept;

  /// @brief RTC video source fed by this capture source; use it with
  /// LocalVideoTrack::createLocalVideoTrack().
  /// @return The RTC video source.
  std::shared_ptr<VideoSource> videoSource() const noexcept;

  /// @brief Returns publish options for this track, applying application options.
  ///
  /// Encoded sources override codec with the source codec, select the
  /// PreEncoded encoder backend, and disable simulcast. All other fields are
  /// taken from @p options unchanged. Pixel sources preserve every application
  /// option; Rust supplies defaults for fields left unset.
  /// @param options Application publish options.
  /// @return Options with source-required fields applied.
  TrackPublishOptions publishOptions(TrackPublishOptions options = {}) const;

  /// @brief Start pumping frames into the RTC video source.
  ///
  /// @return A successful Result when Rust acknowledges the request.
  /// @throws std::runtime_error If the SDK is not initialized, Rust rejects the
  /// request, or transport or protocol validation fails.
  /// @throws std::bad_alloc If memory allocation fails.
  [[nodiscard]] Result<void, CaptureError> start();

  /// @brief Signal a running capture to stop after the frame in flight.
  ///
  /// This method does not wait for the pump thread to exit. Call @ref join()
  /// to synchronize and retrieve the terminal result. Stopping an
  /// already-finished capture is a no-op.
  ///
  /// @return A successful Result when Rust acknowledges the request.
  /// @throws std::runtime_error If the SDK is not initialized, Rust rejects the
  /// request, or transport or protocol validation fails.
  /// @throws std::bad_alloc If memory allocation fails.
  [[nodiscard]] Result<void, CaptureError> stop();

  /// @brief Consume the running pump and wait asynchronously for it to exit.
  ///
  /// This method does not signal the pump to stop. Call @ref stop() first when
  /// ending an unbounded source. Join may be called only once after a
  /// successful @ref start().
  ///
  /// @return A future containing statistics on success or a structured pump
  /// CaptureError whose kind is supplied by Rust.
  /// @throws std::runtime_error When awaiting the future if initialization,
  /// transport, protocol validation, or cancellation fails.
  /// @throws std::bad_alloc If allocation fails, including when awaiting the future.
  [[nodiscard]] std::future<Result<CaptureResult, CaptureError>> join();

private:
  friend class livekit::FfiClient;

  CaptureSource();

  /// @brief Builds the wrapper from the callback payload, adopting its handles.
  /// @param owned Rust-owned source handles and metadata.
  /// @return The capture source wrapper.
  /// @throws std::runtime_error If the source metadata is invalid.
  static std::shared_ptr<CaptureSource> fromOwned(const proto::OwnedCaptureSource& owned);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace livekit::capture
