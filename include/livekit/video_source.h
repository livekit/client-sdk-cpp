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

#include <cstdint>

#include "livekit/ffi_handle.h"

namespace livekit {

class LKVideoFrame;

/**
 * Rotation of a video frame.
 *
 * Mirrors proto_video.VideoRotation but kept as a public SDK enum.
 */
enum class VideoRotation {
  VIDEO_ROTATION_0 = 0,
  VIDEO_ROTATION_90 = 90,
  VIDEO_ROTATION_180 = 180,
  VIDEO_ROTATION_270 = 270,
};

/**
 * Represents a real-time video source that can accept frames from the
 * application and feed them into the LiveKit core.
 */
class VideoSource {
public:
  /**
   * Create a new native video source with a fixed resolution.
   *
   * @param width   Width in pixels.
   * @param height  Height in pixels.
   *
   * Throws std::runtime_error if the FFI call fails or the response
   * does not contain the expected new_video_source field.
   */
  VideoSource(int width, int height);

  // Owned FFI handle will be released by FfiHandle's destructor.
  ~VideoSource() = default;

  VideoSource(const VideoSource &) = delete;
  VideoSource &operator=(const VideoSource &) = delete;
  VideoSource(VideoSource &&) noexcept = default;
  VideoSource &operator=(VideoSource &&) noexcept = default;

  /// Source resolution as declared at construction.
  int width() const noexcept { return width_; }
  int height() const noexcept { return height_; }

  /// Underlying FFI handle ID (0 if invalid).
  std::uint64_t ffi_handle_id() const noexcept { return handle_.get(); }

  /**
   * Push a LKVideoFrame into the FFI video source.
   *
   * @param frame         Video frame to send.
   * @param timestamp_us  Optional timestamp in microseconds.
   * @param rotation      Video rotation enum.
   * @param timeout_ms    Controls waiting behavior:
   *
   * Notes:
   *   - Fire-and-forget to send a frame to FFI
   *     lifetime correctly (e.g., persistent frame pools, GPU buffers, etc.).
   */
  void captureFrame(const LKVideoFrame &frame, std::int64_t timestamp_us = 0,
                    VideoRotation rotation = VideoRotation::VIDEO_ROTATION_0);

private:
  FfiHandle handle_; // owned FFI handle
  int width_{0};
  int height_{0};
};

} // namespace livekit
