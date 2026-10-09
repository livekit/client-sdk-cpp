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
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "livekit/build.h"
#include "livekit/room_event_types.h"
#include "livekit/video_source.h"
#include "livekit/visibility.h"

namespace livekit {

namespace proto {
class NewCaptureSourceRequest;
class OwnedCaptureSource;
} // namespace proto

/// @brief Error raised when capture source creation or control fails.
class LIVEKIT_API CaptureSourceError : public std::runtime_error {
public:
  /// @brief Create a capture source error.
  ///
  /// @param message  Human-readable error message.
  explicit CaptureSourceError(const std::string& message) : std::runtime_error(message) {}
};

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

/// @brief Terminal result of a started capture.
struct CaptureResult {
  /// @brief Error message when the capture failed; empty on success.
  std::optional<std::string> error;

  /// @brief Number of frames or access units captured.
  std::uint64_t frames_captured = 0;

  /// Why the capture ended; meaningful only when @ref error is empty.
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
/// Requires a capture-enabled SDK build (`LIVEKIT_ENABLE_CAPTURE`), with
/// `capture-gstreamer`, `capture-pattern`, and `capture-clock` Rust features.
/// Otherwise creation fails.
///
/// @note Keep this object alive while the track is published. Destroying it
/// stops a running capture.
class LIVEKIT_API CaptureSource {
public:
  /// @brief Terminal notification for a started capture, invoked exactly once on
  /// the FFI event thread (like room delegate callbacks).
  using FinishedCallback = std::function<void(const CaptureResult&)>;

  /// @brief Create a capture source from a GStreamer pipeline configuration.
  ///
  /// Completes asynchronously: construction starts the pipeline and may wait
  /// for its first output to discover stream settings. Errors (invalid
  /// pipeline, missing capture feature, discovery timeout) are thrown from
  /// the future as @ref CaptureSourceError.
  ///
  /// @param config GStreamer pipeline configuration.
  /// @return A future that resolves to the created capture source.
  /// @throws CaptureSourceError When awaiting the future if creation fails.
  static std::future<std::shared_ptr<CaptureSource>> create(GstreamerVideoSourceConfig config);

  /// @brief Create the built-in test pattern capture source.
  ///
  /// @param config Pattern source configuration.
  /// @return A future that resolves to the created capture source.
  /// @throws CaptureSourceError When awaiting the future if creation fails.
  static std::future<std::shared_ptr<CaptureSource>> create(PatternVideoSourceConfig config);

  /// @brief Create the built-in wall clock capture source.
  /// @param config Clock source configuration.
  /// @return A future that resolves to the created capture source.
  /// @throws CaptureSourceError When awaiting the future if creation fails.
  static std::future<std::shared_ptr<CaptureSource>> create(ClockVideoSourceConfig config);

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
  /// Fields the source dictates (e.g. codec, encoder backend, and simulcast
  /// for encoded sources) are required for correct publication and override
  /// the application values; all other fields are taken from @p options
  /// unchanged.
  /// @param options Application publish options.
  /// @return Options with source-required fields applied.
  TrackPublishOptions publishOptions(TrackPublishOptions options = {}) const;

  /// @brief Set the terminal notification. Set this before @ref start().
  /// @param callback Terminal notification, called on the FFI callback thread.
  void setOnFinishedCallback(FinishedCallback callback);

  /// @brief Start pumping frames into the RTC video source.
  ///
  /// @return @c true if the start request succeeds; otherwise @c false.
  bool start();

  /// @brief Signal a running capture to stop after the frame in flight. The
  /// finished callback fires shortly after. Stopping an already-finished
  /// capture is a no-op.
  ///
  /// @return @c true if the stop request succeeds; otherwise @c false.
  bool stop();

private:
  CaptureSource();

  /// @brief Shared creation path: sends the request and maps the callback payload
  /// onto a wrapper instance.
  static std::future<std::shared_ptr<CaptureSource>> createFromRequest(proto::NewCaptureSourceRequest request);

  /// @brief Builds the wrapper from the callback payload, adopting its handles.
  static std::shared_ptr<CaptureSource> fromOwned(const proto::OwnedCaptureSource& owned);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace livekit
