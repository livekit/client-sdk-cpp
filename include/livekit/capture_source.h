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
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

#include "livekit/build.h"
#include "livekit/ffi_handle.h"
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
  /// Pixel frames, published through the WebRTC encoder.
  Pixel = 0,
  /// Pre-encoded access units, published as passthrough.
  Encoded = 1,
};

/// @brief Video resolution in pixels.
struct CaptureResolution {
  int width = 0;
  int height = 0;
};

/// @brief Test pattern rendered by the built-in pattern source.
enum class Pattern {
  /// Animated color gradient.
  Gradient = 0,
  /// Bouncing LiveKit logo.
  Logo = 1,
};

/// @brief Configuration for the built-in test source rendering a @ref Pattern.
struct PatternVideoSourceConfig {
  /// Output resolution; both components must be non-zero.
  CaptureResolution resolution;

  /// Output frame rate in frames per second; must be non-zero.
  std::uint32_t framerate_fps = 0;

  /// Pattern to render.
  Pattern pattern = Pattern::Gradient;
};

/// @brief Configuration for the built-in clock source, showing local time.
struct ClockVideoSourceConfig {
  /// Output resolution; both components must be non-zero.
  CaptureResolution resolution;

  /// Output frame rate in frames per second; must be non-zero.
  std::uint32_t framerate_fps = 0;
};

/// @brief Why a capture ended without error.
enum class CaptureExit {
  /// Stopped via @ref CaptureSource::stop (or source destruction).
  Stopped = 0,
  /// The producer reached the end of its stream.
  EndOfStream = 1,
};

/// @brief Terminal result of a started capture.
struct CaptureResult {
  /// Error message when the capture failed; empty on success.
  std::optional<std::string> error;

  /// Number of frames or access units captured.
  std::uint64_t frames_captured = 0;

  /// Why the capture ended; meaningful only when @ref error is empty.
  CaptureExit exit = CaptureExit::Stopped;
};

/// @brief A server-side capture source (livekit-capture) that produces video
/// without per-frame FFI traffic.
///
/// The source owns its producer and the pump that feeds an RTC video source.
/// Publish it like any other source: create
/// a track from @ref CaptureSource::videoSource(), merge application options
/// through @ref CaptureSource::publishOptions(), then call @ref CaptureSource::start().
///
/// Requires the FFI library to be built with the `capture-pattern` and
/// `capture-clock` features (`LIVEKIT_ENABLE_CAPTURE`); otherwise creation fails.
///
/// @note Keep this object alive while the track is published. Destroying it
/// stops a running capture.
class LIVEKIT_API CaptureSource {
public:
  /// @brief Terminal notification for a started capture, invoked exactly once on
  /// the FFI event thread (like room delegate callbacks).
  using FinishedCallback = std::function<void(const CaptureResult&)>;

  /// @brief Create the built-in test pattern capture source.
  ///
  /// @param config Pattern source configuration.
  /// @return A future that resolves to the created capture source.
  /// @throws CaptureSourceError When awaiting the future if creation fails.
  static std::future<std::shared_ptr<CaptureSource>> create(PatternVideoSourceConfig config);

  /// @brief Create the built-in clock capture source.
  ///
  /// The clock shows the local time of the machine running the Rust FFI.
  ///
  /// @param config Clock source configuration.
  /// @return A future that resolves to the created capture source.
  /// @throws CaptureSourceError When awaiting the future if creation fails.
  static std::future<std::shared_ptr<CaptureSource>> create(ClockVideoSourceConfig config);

  /// @brief Destroy the source, stopping an active capture.
  ~CaptureSource();

  CaptureSource(const CaptureSource&) = delete;
  CaptureSource& operator=(const CaptureSource&) = delete;
  CaptureSource(CaptureSource&&) = delete;
  CaptureSource& operator=(CaptureSource&&) = delete;

  /// @brief Return the kind of media this source produces.
  /// @return The source kind.
  CaptureSourceKind kind() const noexcept { return kind_; }

  /// @brief Return the stream width.
  /// @return Width in pixels.
  int width() const noexcept { return width_; }

  /// @brief Return the stream height.
  /// @return Height in pixels.
  int height() const noexcept { return height_; }

  /// @brief Return the codec produced by an encoded source.
  /// @return The codec, or no value for a pixel source.
  std::optional<VideoCodec> codec() const noexcept { return codec_; }

  /// @brief Return the RTC video source fed by this capture source.
  ///
  /// Use it with
  /// LocalVideoTrack::createLocalVideoTrack().
  /// @return The source to use when creating a local video track.
  std::shared_ptr<VideoSource> videoSource() const noexcept { return video_source_; }

  /// @brief Return publish options for this track, applying application options.
  ///
  /// Recommended fields fill unset application fields. For encoded sources,
  /// codec, encoder backend, and simulcast are required for publication and
  /// override the application values. Other fields are taken from @p options.
  /// @param options Application publish options.
  /// @return Merged options for publishing the track.
  TrackPublishOptions publishOptions(TrackPublishOptions options = {}) const;

  /// @brief Set the terminal notification before calling @ref start().
  /// @param callback Function called on the FFI event thread when capture ends.
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
  CaptureSource() = default;

  /// @brief Shared creation path: sends the request and maps the callback payload
  /// onto a wrapper instance.
  /// @param request Source creation request.
  /// @return A future containing the new source.
  static std::future<std::shared_ptr<CaptureSource>> createFromRequest(proto::NewCaptureSourceRequest request);

  /// @brief Build the wrapper from the callback payload, adopting its handles.
  /// @param owned Rust-owned capture source descriptor.
  /// @return The new C++ wrapper.
  static std::shared_ptr<CaptureSource> fromOwned(const proto::OwnedCaptureSource& owned);

  FfiHandle handle_;
  CaptureSourceKind kind_ = CaptureSourceKind::Pixel;
  int width_ = 0;
  int height_ = 0;
  std::optional<VideoCodec> codec_;
  std::shared_ptr<VideoSource> video_source_;
  /// Fields set here are dictated by the source and win in publishOptions().
  TrackPublishOptions source_publish_options_;

  std::mutex callback_mutex_;
  std::shared_ptr<const FinishedCallback> on_finished_;
  int listener_id_ = 0;
};

} // namespace livekit
