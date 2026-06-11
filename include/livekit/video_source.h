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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "livekit/ffi_handle.h"
#include "livekit/room_event_types.h"
#include "livekit/visibility.h"

namespace livekit {

namespace proto {
class FfiEvent;
}

class VideoFrame;

/// Rotation of a video frame.
///
/// Mirrors proto_video.VideoRotation but kept as a public SDK enum.
enum class VideoRotation {
  VIDEO_ROTATION_0 = 0,
  VIDEO_ROTATION_90 = 90,
  VIDEO_ROTATION_180 = 180,
  VIDEO_ROTATION_270 = 270,
};

/// Optional packet-trailer metadata carried alongside a video frame.
///
/// Each field is independently optional because the corresponding transport
/// feature can be negotiated separately.
struct VideoFrameMetadata {
  std::optional<std::uint64_t> user_timestamp_us;
  std::optional<std::uint32_t> frame_id;
};

/// Capture options for a single outbound video frame.
struct VideoCaptureOptions {
  std::int64_t timestamp_us = 0;
  VideoRotation rotation = VideoRotation::VIDEO_ROTATION_0;
  /// Populate meta data when you want to send user timestamps or frame IDs.
  std::optional<VideoFrameMetadata> metadata;
};

struct EncodedVideoSourceOptions {
  VideoCodec codec = VideoCodec::H264;
};

struct EncodedVideoFrameInfo {
  bool is_keyframe = false;
  bool has_sps_pps = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::int64_t capture_time_us = 0;
};

class EncodedVideoSourceObserver {
public:
  virtual ~EncodedVideoSourceObserver() = default;

  virtual void onKeyframeRequested() {}
  virtual void onTargetBitrate(std::uint32_t bitrate_bps, double framerate_fps) {
    (void)bitrate_bps;
    (void)framerate_fps;
  }
};

/**
 * Represents a real-time video source that can accept frames from the
 * application and feed them into the LiveKit core.
 */
class LIVEKIT_API VideoSource {
public:
  /// Create a new native video source with a fixed resolution.
  ///
  /// @param width   Width in pixels.
  /// @param height  Height in pixels.
  ///
  /// @throws std::runtime_error if the FFI call fails or the response
  ///         does not contain the expected new_video_source field.
  VideoSource(int width, int height);
  VideoSource(int width, int height, const EncodedVideoSourceOptions& encoded_options);
  virtual ~VideoSource();

  VideoSource(const VideoSource&) = delete;
  VideoSource& operator=(const VideoSource&) = delete;
  VideoSource(VideoSource&& other) noexcept;
  VideoSource& operator=(VideoSource&& other) noexcept;

  /// Source resolution as declared at construction.
  int width() const noexcept { return width_; }
  int height() const noexcept { return height_; }

  /// Underlying FFI handle ID (0 if invalid).
  std::uint64_t ffiHandleId() const noexcept { return handle_.get(); }

  /// Push a VideoFrame into the FFI video source.
  ///
  /// @param frame    Video frame to send.
  /// @param options  Timestamp, rotation, and optional metadata for this frame.
  void captureFrame(const VideoFrame& frame, const VideoCaptureOptions& options);

  /// Backward-compatible convenience overload for timestamp + rotation only.
  void captureFrame(const VideoFrame& frame, std::int64_t timestamp_us = 0,
                    VideoRotation rotation = VideoRotation::VIDEO_ROTATION_0);

  /**
   * Push an encoded frame into an encoded video source.
   *
   * The source must have been created with EncodedVideoSourceOptions. Returns
   * true when the frame was queued by the Rust/WebRTC layer and false when it
   * was dropped because the internal queue was full.
   */
  bool captureEncodedFrame(const std::uint8_t* data, std::size_t size, const EncodedVideoFrameInfo& info);

  bool captureEncodedFrame(const std::vector<std::uint8_t>& data, const EncodedVideoFrameInfo& info) {
    return captureEncodedFrame(data.data(), data.size(), info);
  }

  void setEncodedObserver(std::shared_ptr<EncodedVideoSourceObserver> observer);

private:
  void registerEncodedListener();
  void unregisterEncodedListener() noexcept;
  void handleEncodedEvent(const proto::FfiEvent& event) const;

  FfiHandle handle_; // owned FFI handle
  int width_{0};
  int height_{0};
  bool encoded_{false};
  int encoded_listener_id_{0};
  mutable std::mutex encoded_observer_lock_;
  std::shared_ptr<EncodedVideoSourceObserver> encoded_observer_;
};

} // namespace livekit
