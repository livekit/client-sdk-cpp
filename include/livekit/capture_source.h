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

#include "livekit/ffi_handle.h"
#include "livekit/room_event_types.h"
#include "livekit/video_source.h"
#include "livekit/visibility.h"

namespace livekit {

namespace proto {
class NewCaptureSourceRequest;
class OwnedCaptureSource;
} // namespace proto

/// Error raised when capture source creation or control fails.
class LIVEKIT_API CaptureSourceError : public std::runtime_error {
public:
  /// Create a capture source error.
  ///
  /// @param message  Human-readable error message.
  explicit CaptureSourceError(const std::string& message) : std::runtime_error(message) {}
};

/// Kind of media a capture source produces.
enum class CaptureSourceKind {
  /// Pixel frames, published through the WebRTC encoder.
  Pixel = 0,
  /// Pre-encoded access units, published as passthrough.
  Encoded = 1,
};

/// Bitrate unit expected by a GStreamer encoder property.
enum class GstreamerBitrateUnit {
  /// Bits per second.
  Bps = 0,
  /// Kilobits per second.
  Kbps = 1,
};

/// Video resolution in pixels.
struct CaptureResolution {
  int width = 0;
  int height = 0;
};

/// Binding from WebRTC rate-control targets to a GStreamer encoder property.
struct GstreamerRateControl {
  /// Name of the encoder element in the pipeline (e.g. `lk_encoder`).
  std::string element;

  /// Bitrate property to set on the element (e.g. `bitrate` for x264enc,
  /// `target-bitrate` for vp8enc/vp9enc).
  std::string property;

  /// Unit the property expects.
  GstreamerBitrateUnit unit = GstreamerBitrateUnit::Bps;
};

/// Configuration for encoded ingest from a GStreamer pipeline.
struct GstreamerVideoSourceConfig {
  /// GStreamer launch description for the encoded producer pipeline.
  ///
  /// Must contain `appsink name=lk_appsink`, or leave exactly one encoded
  /// video source pad unlinked for the source to attach one to.
  std::string pipeline;

  /// Codec expected from the pipeline; inferred from pipeline caps when
  /// omitted.
  std::optional<VideoCodec> codec;

  /// Encoded frame resolution. When omitted, it is discovered from the
  /// pipeline's negotiated caps; when set, the pipeline output is verified
  /// against it.
  std::optional<CaptureResolution> resolution;

  /// Forwards WebRTC rate-control targets to an encoder element's bitrate
  /// property. Without this, the pipeline encodes at a fixed bitrate.
  std::optional<GstreamerRateControl> rate_control;
};

/// Configuration for the built-in test source producing solid-color frames.
struct DemoVideoSourceConfig {
  /// Output resolution; both components must be non-zero.
  CaptureResolution resolution;

  /// Output frame rate in frames per second; must be non-zero.
  std::uint32_t framerate_fps = 0;
};

/// Why a capture ended without error.
enum class CaptureExit {
  /// Stopped via @ref CaptureSource::stop (or source destruction).
  Stopped = 0,
  /// The producer reached the end of its stream.
  EndOfStream = 1,
};

/// Terminal result of a started capture.
struct CaptureResult {
  /// Error message when the capture failed; empty on success.
  std::optional<std::string> error;

  /// Number of frames or access units captured.
  std::uint64_t frames_captured = 0;

  /// Why the capture ended; meaningful only when @ref error is empty.
  CaptureExit exit = CaptureExit::Stopped;
};

/// A server-side capture source (livekit-capture) that produces video
/// without per-frame FFI traffic.
///
/// The source owns its producer (e.g. a GStreamer pipeline) and the pump
/// that feeds an RTC video source. Publish it like any other source: create
/// a track from @ref videoSource(), publish with application options merged
/// over @ref recommendedPublishOptions(), then call @ref start().
///
/// Requires the FFI library to be built with the `capture` feature
/// (`LIVEKIT_ENABLE_CAPTURE`); otherwise creation fails.
///
/// @note Keep this object alive while the track is published. Destroying it
/// stops a running capture.
class LIVEKIT_API CaptureSource {
public:
  /// Terminal notification for a started capture, invoked exactly once on
  /// the FFI event thread (like room delegate callbacks).
  using FinishedCallback = std::function<void(const CaptureResult&)>;

  /// Create a capture source from a GStreamer pipeline configuration.
  ///
  /// Completes asynchronously: construction starts the pipeline and may wait
  /// for its first output to discover stream settings. Errors (invalid
  /// pipeline, missing capture feature, discovery timeout) are thrown from
  /// the future as @ref CaptureSourceError.
  static std::future<std::shared_ptr<CaptureSource>> create(GstreamerVideoSourceConfig config);

  /// Create the built-in demo capture source (solid cycling colors).
  static std::future<std::shared_ptr<CaptureSource>> create(DemoVideoSourceConfig config);

  ~CaptureSource();

  CaptureSource(const CaptureSource&) = delete;
  CaptureSource& operator=(const CaptureSource&) = delete;
  CaptureSource(CaptureSource&&) = delete;
  CaptureSource& operator=(CaptureSource&&) = delete;

  /// Kind of media this source produces.
  CaptureSourceKind kind() const noexcept { return kind_; }

  /// Declared or discovered stream resolution.
  int width() const noexcept { return width_; }
  int height() const noexcept { return height_; }

  /// Codec produced by the source; encoded sources only.
  std::optional<VideoCodec> codec() const noexcept { return codec_; }

  /// RTC video source fed by this capture source; use it with
  /// LocalVideoTrack::createLocalVideoTrack().
  std::shared_ptr<VideoSource> videoSource() const noexcept { return video_source_; }

  /// Returns publish options for this track, applying application options.
  ///
  /// Fields the source dictates (e.g. codec, encoder backend, and simulcast
  /// for encoded sources) are required for correct publication and override
  /// the application values; all other fields are taken from @p options
  /// unchanged.
  TrackPublishOptions publishOptions(TrackPublishOptions options = {}) const;

  /// Set the terminal notification. Set this before @ref start().
  void setOnFinishedCallback(FinishedCallback callback);

  /// Start pumping frames into the RTC video source.
  ///
  /// @throws CaptureSourceError if the capture was already started or the
  ///         FFI call fails.
  void start();

  /// Signal a running capture to stop after the frame in flight. The
  /// finished callback fires shortly after. Stopping an already-finished
  /// capture is a no-op.
  ///
  /// @throws CaptureSourceError if the FFI call fails.
  void stop();

private:
  CaptureSource() = default;

  /// Shared creation path: sends the request and maps the callback payload
  /// onto a wrapper instance.
  static std::future<std::shared_ptr<CaptureSource>> createFromRequest(proto::NewCaptureSourceRequest request);

  /// Builds the wrapper from the callback payload, adopting its handles.
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
  FinishedCallback on_finished_;
  int listener_id_ = 0;
};

} // namespace livekit
