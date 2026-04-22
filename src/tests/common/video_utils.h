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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <livekit/livekit.h>
#include <memory>
#include <thread>

namespace livekit::test {

// Default video dimensions for tests
constexpr int kDefaultVideoWidth = 640;
constexpr int kDefaultVideoHeight = 360;

/// Fill a VideoFrame with webcam-like colors (green tint with varying blue).
/// @param frame The VideoFrame to fill (must be ARGB format).
/// @param frame_index Frame counter used to vary the blue channel.
inline void fillWebcamLikeFrame(VideoFrame &frame, std::uint64_t frame_index) {
  // ARGB layout: [A, R, G, B]
  std::uint8_t *data = frame.data();
  const std::size_t size = frame.dataSize();
  const std::uint8_t blue = static_cast<std::uint8_t>((frame_index * 3) % 255);
  for (std::size_t i = 0; i < size; i += 4) {
    data[i + 0] = 255; // A
    data[i + 1] = 0;   // R
    data[i + 2] = 170; // G
    data[i + 3] = blue;
  }
}

/// Fill a VideoFrame with solid red and encode metadata in the first 16 pixels.
/// @param frame The VideoFrame to fill (must be ARGB format).
/// @param frame_index Frame counter encoded into metadata.
/// @param timestamp_us Timestamp in microseconds encoded into metadata.
inline void fillRedFrameWithMetadata(VideoFrame &frame,
                                     std::uint64_t frame_index,
                                     std::uint64_t timestamp_us) {
  // ARGB layout: [A, R, G, B]
  std::uint8_t *data = frame.data();
  const std::size_t size = frame.dataSize();
  for (std::size_t i = 0; i < size; i += 4) {
    data[i + 0] = 255; // A
    data[i + 1] = 255; // R
    data[i + 2] = 0;   // G
    data[i + 3] = 0;   // B
  }

  // Encode frame counter + timestamp into first 16 pixels for easy debugging.
  std::uint8_t meta[16];
  for (int i = 0; i < 8; ++i) {
    meta[i] = static_cast<std::uint8_t>((frame_index >> (i * 8)) & 0xFF);
    meta[8 + i] = static_cast<std::uint8_t>((timestamp_us >> (i * 8)) & 0xFF);
  }
  for (int i = 0; i < 16; ++i) {
    const std::size_t px = static_cast<std::size_t>(i) * 4;
    if (px + 3 < size) {
      data[px + 0] = 255;
      data[px + 1] = 255;
      data[px + 2] = meta[i];
      data[px + 3] = meta[(15 - i)];
    }
  }
}

/// Wrapper for fillWebcamLikeFrame with timestamp parameter (ignored).
inline void fillWebcamWrapper(VideoFrame &frame, std::uint64_t frame_index,
                              std::uint64_t /*timestamp_us*/) {
  fillWebcamLikeFrame(frame, frame_index);
}

/// Wrapper for fillRedFrameWithMetadata.
inline void fillRedWrapper(VideoFrame &frame, std::uint64_t frame_index,
                           std::uint64_t timestamp_us) {
  fillRedFrameWithMetadata(frame, frame_index, timestamp_us);
}

/// Type alias for video frame fill functions.
using VideoFrameFillFn = void (*)(VideoFrame &, std::uint64_t, std::uint64_t);

/// Run a video capture loop, calling the fill function for each frame.
/// @param source The VideoSource to capture frames to.
/// @param running Atomic flag to control when to stop the loop.
/// @param fill_fn Function to fill each frame with content.
/// @param width Video frame width (default: kDefaultVideoWidth).
/// @param height Video frame height (default: kDefaultVideoHeight).
/// @param frame_interval Frame interval (default: 33ms for ~30fps).
inline void runVideoLoop(
    const std::shared_ptr<VideoSource> &source, std::atomic<bool> &running,
    VideoFrameFillFn fill_fn, int width = kDefaultVideoWidth,
    int height = kDefaultVideoHeight,
    std::chrono::milliseconds frame_interval = std::chrono::milliseconds(33)) {
  VideoFrame frame = VideoFrame::create(width, height, VideoBufferType::ARGB);
  std::uint64_t frame_index = 0;
  while (running.load(std::memory_order_relaxed)) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ts_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
    fill_fn(frame, frame_index, ts_us);
    try {
      source->captureFrame(frame, static_cast<std::int64_t>(ts_us),
                           VideoRotation::VIDEO_ROTATION_0);
    } catch (...) {
      break;
    }
    frame_index++;
    std::this_thread::sleep_for(frame_interval);
  }
}

} // namespace livekit::test
