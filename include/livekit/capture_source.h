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
#include <vector>

#include "livekit/ffi_handle.h"
#include "livekit/room_event_types.h"
#include "livekit/video_source.h"
#include "livekit/visibility.h"

namespace livekit {

namespace proto {
class NewCaptureSourceRequest;
class OwnedCaptureSource;
class CaptureDeviceList;
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
  /// @note Must contain `appsink name=lk_appsink`, or leave exactly one encoded
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

/// Test pattern rendered by the built-in pattern source.
enum class Pattern {
  /// Animated color gradient.
  Gradient = 0,
  /// Bouncing LiveKit logo.
  Logo = 1,
};

/// Configuration for the built-in test source rendering a @ref Pattern.
struct PatternVideoSourceConfig {
  /// Output resolution; both components must be non-zero.
  CaptureResolution resolution;

  /// Output frame rate in frames per second; must be non-zero.
  std::uint32_t framerate_fps = 0;

  /// Pattern to render.
  Pattern pattern = Pattern::Gradient;
};

/// Frame format delivered by a capture device.
enum class DeviceFrameFormat {
  /// Planar I420/YUV420P.
  I420 = 0,
  /// Biplanar NV12.
  NV12 = 1,
  /// Packed BGRA.
  BGRA = 2,
  /// Packed RGB24.
  RGB24 = 3,
  /// Packed BGR24.
  BGR24 = 4,
  /// Packed YUYV/YUY2.
  YUYV = 5,
  /// Packed UYVY.
  UYVY = 6,
  /// Single-plane 8-bit luma.
  GREY = 7,
  /// Encoded MJPEG frames.
  MJPEG = 8,
};

/// Capture format offered by or requested from a device.
///
/// Width, height, and frame rate must be non-zero. The frame format defaults
/// to @ref DeviceFrameFormat::NV12, the only value accepted by both device
/// backends.
struct DeviceFormat {
  /// Frame dimensions.
  CaptureResolution resolution;

  /// Frame rate in frames per second.
  std::uint32_t framerate_fps = 0;

  /// Frame format.
  DeviceFrameFormat frame_format = DeviceFrameFormat::NV12;

  DeviceFormat() = default;

  DeviceFormat(CaptureResolution resolution, std::uint32_t framerate_fps, DeviceFrameFormat frame_format)
      : resolution(resolution), framerate_fps(framerate_fps), frame_format(frame_format) {}
};

/// How a capture device should pick the format it delivers.
///
/// The device negotiates the delivered format; the created source reports the
/// negotiated resolution through @ref CaptureSource::width and
/// @ref CaptureSource::height. The negotiated frame rate and frame format are
/// not reported back.
///
/// @note Only resolution and frame rate participate in format selection.
/// A @c frame_format is validated and then treated as a preference the backend
/// may substitute, so the delivered format can differ from the one requested:
///
/// - macOS accepts only @ref DeviceFrameFormat::I420,
///   @ref DeviceFrameFormat::NV12, and @ref DeviceFrameFormat::BGRA --
///   requesting any other value fails creation -- and then delivers NV12
///   regardless of which of the three was asked for.
/// - V4L2 rejects @ref DeviceFrameFormat::I420 and @ref DeviceFrameFormat::BGRA.
///   For @ref exact and @ref closest it tries the requested format first and
///   then falls back through the formats it supports; for @ref highestFramerate
///   and @ref highestResolution the constraint selects the format outright, with
///   no fallback.
///
/// @ref DeviceFrameFormat::NV12 is the only value both backends accept, which
/// is why it is @ref DeviceFormat's default.
class LIVEKIT_API DeviceFormatRequest {
public:
  /// Optional constraints for @ref highestFramerate.
  struct HighestFramerateConstraint {
    /// Only consider formats with this resolution.
    std::optional<CaptureResolution> resolution;
    /// Only consider formats with this frame format.
    std::optional<DeviceFrameFormat> frame_format;
  };

  /// Optional constraints for @ref highestResolution.
  struct HighestResolutionConstraint {
    /// Only consider formats with this frame rate.
    std::optional<std::uint32_t> framerate_fps;
    /// Only consider formats with this frame format.
    std::optional<DeviceFrameFormat> frame_format;
  };

  /// Let the device choose its default format.
  DeviceFormatRequest() = default;

  /// Require the resolution to match exactly; creation fails if the device
  /// offers no such format.
  ///
  /// Frame-rate matching is rounding-tolerant, because devices commonly
  /// advertise near-integral rates: a request for 30 fps is satisfied by a
  /// device advertising 30.00003 fps, and the device is then driven at its
  /// advertised rate.
  static DeviceFormatRequest exact(DeviceFormat format);

  /// Use the device's closest supported resolution and frame rate.
  static DeviceFormatRequest closest(DeviceFormat format);

  /// Prefer the highest frame rate, optionally constrained.
  static DeviceFormatRequest highestFramerate(HighestFramerateConstraint constraint = {});

  /// Prefer the highest resolution, optionally constrained.
  static DeviceFormatRequest highestResolution(HighestResolutionConstraint constraint = {});

private:
  friend class CaptureSource;

  /// Which selection strategy this request carries.
  enum class Kind { Default, Exact, Closest, HighestFramerate, HighestResolution };

  Kind kind_ = Kind::Default;
  DeviceFormat format_;
  HighestFramerateConstraint highest_framerate_;
  HighestResolutionConstraint highest_resolution_;
};

/// Selects which video device a capture source opens.
class LIVEKIT_API DeviceSelector {
public:
  /// The platform default video device.
  DeviceSelector() = default;

  /// The device at this position in the platform's own enumeration order.
  ///
  /// The meaning is platform-defined and is not necessarily the position of
  /// the device in @ref CaptureSource::listDevices: on macOS it indexes the
  /// AVFoundation video-device list, on Linux it is the V4L2 node number
  /// (@c /dev/videoN). Either way the value can shift as devices are attached
  /// and removed. Prefer @ref id, which is stable on macOS.
  static DeviceSelector index(std::uint32_t device_index);

  /// A platform-stable identifier, as reported by @ref CaptureDeviceInfo::id.
  static DeviceSelector id(std::string device_id);

private:
  friend class CaptureSource;

  /// Which form of device selection this carries.
  enum class Kind { Default, Index, Id };

  Kind kind_ = Kind::Default;
  std::uint32_t index_ = 0;
  std::string id_;
};

/// Configuration for camera device capture using the platform's native
/// capture stack.
struct DeviceVideoSourceConfig {
  /// Device to capture from; the platform default device when unset.
  DeviceSelector device;

  /// Format requested from the device; the device default when unset.
  DeviceFormatRequest format;
};

/// A video capture device discovered by @ref CaptureSource::listDevices.
struct CaptureDeviceInfo {
  /// Device identifier; pass to @ref DeviceSelector::id.
  ///
  /// Stable across reboots and re-plugging on macOS, where it is the
  /// AVFoundation unique id. On V4L2 it is the node number, so it can change
  /// as devices are attached and removed; re-enumerate rather than persisting
  /// it there.
  std::string id;

  /// Human-readable device name.
  std::string name;

  /// Device model identifier, when available.
  std::optional<std::string> model_id;

  /// Device manufacturer, when available.
  std::optional<std::string> manufacturer;

  /// Capture formats reported by the device.
  ///
  /// Empty unless @ref formats_complete is true. Do not use this to decide
  /// what to request: ask for a format and let the device negotiate.
  std::vector<DeviceFormat> formats;

  /// Whether @ref formats is a complete list; some platforms do not
  /// enumerate formats up front.
  ///
  /// False on macOS, where AVFoundation formats are not enumerated; true on
  /// V4L2.
  bool formats_complete = false;
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

  /// Create the built-in test pattern capture source.
  static std::future<std::shared_ptr<CaptureSource>> create(PatternVideoSourceConfig config);

  /// Create a capture source from a camera device.
  ///
  /// Completes asynchronously: construction opens the device and negotiates
  /// the capture format, so @ref width and @ref height report the negotiated
  /// resolution before the first frame is pumped. Errors (missing device,
  /// unsatisfiable
  /// format request, unsupported platform, missing capture feature) are
  /// thrown from the future as @ref CaptureSourceError.
  static std::future<std::shared_ptr<CaptureSource>> create(DeviceVideoSourceConfig config);

  /// List the video capture devices available on this machine.
  ///
  /// Completes asynchronously: enumeration queries the platform capture stack
  /// and may block briefly. Throws @ref CaptureSourceError from the future on
  /// failure, including on platforms without a capture backend.
  static std::future<std::vector<CaptureDeviceInfo>> listDevices();

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
