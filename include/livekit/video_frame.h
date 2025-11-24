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
#include <optional>
#include <vector>

namespace livekit {

// Mirror of WebRTC video buffer type
enum class VideoBufferType {
  ARGB,
  ABGR,
  RGBA,
  BGRA,
  RGB24,
  I420,
  I420A,
  I422,
  I444,
  I010,
  NV12
};

struct VideoPlaneInfo {
  std::uintptr_t data_ptr; // pointer to plane data (for FFI)
  std::uint32_t stride;    // bytes per row
  std::uint32_t size;      // plane size in bytes
};

/**
 * Public SDK representation of a video frame.
 *
 * - Owns its pixel buffer (std::vector<uint8_t>).
 * - Developers can allocate and fill frames in C++ and pass them to the SDK.
 * - The SDK can expose the backing memory to Rust via data_ptr + layout for
 *   the duration of a blocking FFI call (similar to AudioFrame).
 */
class LKVideoFrame {
public:
  LKVideoFrame() = delete;
  LKVideoFrame(int width, int height, VideoBufferType type,
               std::vector<std::uint8_t> data);

  LKVideoFrame(const LKVideoFrame &) = delete;
  LKVideoFrame &operator=(const LKVideoFrame &) = delete;
  LKVideoFrame(LKVideoFrame &&) noexcept = default;
  LKVideoFrame &operator=(LKVideoFrame &&) noexcept = default;

  /* LKVideoFrame(LKVideoFrame&& other) noexcept
      : width_(other.width_),
        height_(other.height_),
        type_(other.type_),
        data_(std::move(other.data_)) {
      other.width_ = 0;
      other.height_ = 0;
  }
  LKVideoFrame& operator=(LKVideoFrame&& other) noexcept;*/

  /**
   * Allocate a new frame with the correct buffer size for the given format.
   * Data is zero-initialized.
   */
  static LKVideoFrame create(int width, int height, VideoBufferType type);

  // Basic properties
  int width() const noexcept { return width_; }
  int height() const noexcept { return height_; }
  VideoBufferType type() const noexcept { return type_; }

  std::uint8_t *data() noexcept { return data_.data(); }
  const std::uint8_t *data() const noexcept { return data_.data(); }
  std::size_t dataSize() const noexcept { return data_.size(); }

  /**
   * Compute plane layout for this frame (Y/U/V, UV, etc.), in terms of
   * pointers & sizes relative to this frame's backing buffer.
   *
   * For packed formats (ARGB, RGB24) this will be either 1 plane or empty.
   */
  std::vector<VideoPlaneInfo> planeInfos() const;

  /**
   * Convert this frame into another pixel format.
   *
   * This uses the underlying FFI `video_convert` pipeline to transform the
   * current frame into a new `LKVideoFrame` with the requested
   * `dst` buffer type (e.g. ARGB → I420, BGRA → RGB24, etc.).
   *
   * @param dst     Desired output format (see VideoBufferType).
   * @param flip_y  If true, the converted frame will be vertically flipped.
   *
   * @return A new LKVideoFrame containing the converted image data.
   *
   * Notes:
   *  - This function allocates a new buffer and copies pixel data; it does
   *    not modify the original frame.
   *  - This function performs a full CPU-based pixel conversion**. Depending
   *    on resolution and format, this may involve substantial computation
   *    (e.g., color-space transforms, planar repacking, vertical flipping).
   *    Avoid calling this inside tight real-time loops unless necessary.
   *  - Throws std::runtime_error if the FFI conversion fails or if the
   *    format combination is unsupported.
   *
   * Typical usage:
   *        LKVideoFrame i420 = frame.convert(VideoBufferType::I420);
   */
  LKVideoFrame convert(VideoBufferType dst, bool flip_y = false) const;

private:
  int width_;
  int height_;
  VideoBufferType type_;
  std::vector<std::uint8_t> data_;
};

} // namespace livekit
