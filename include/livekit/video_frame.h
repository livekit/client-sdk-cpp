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

#include "livekit/ffi_handle.h"
#include "livekit/visibility.h"

namespace livekit {

/// Mirror of WebRTC video buffer type
enum class VideoBufferType { RGBA = 0, ABGR, ARGB, BGRA, RGB24, I420, I420A, I422, I444, I010, NV12 };

/// @brief Plane layout metadata for a VideoFrame buffer.
struct VideoPlaneInfo {
  std::uintptr_t data_ptr; ///< pointer to plane data (for FFI)
  std::uint32_t stride;    ///< bytes per row
  std::uint32_t size;      ///< plane size in bytes
};

namespace proto {
class OwnedVideoBuffer;
}

/// Public SDK representation of a video frame.
///
/// - Owns its pixel buffer (std::vector<uint8_t>), unless borrowed.
/// - Developers can allocate and fill frames in C++ and pass them to the SDK.
/// - The SDK can expose the backing memory to Rust via data_ptr + layout for
///   the duration of a blocking FFI call (similar to AudioFrame).
///
/// ## Borrowed frames
///
/// A frame delivered by a VideoStream opened with `Options::zero_copy` reads the SDK's own buffer
/// instead of a copy of it, saving a full frame-sized memcpy per frame. Such a frame holds the FFI
/// handle that keeps the buffer alive, so the buffer is returned when the frame is destroyed or
/// release() is called -- plain RAII, and the reason VideoFrame is move-only.
///
/// Every accessor works the same either way. The difference is lifetime: pointers from data() and
/// planeInfos() on a borrowed frame are only valid while that frame is, so do not stash them past
/// its scope.
class LIVEKIT_API VideoFrame {
public:
  VideoFrame();
  VideoFrame(int width, int height, VideoBufferType type, std::vector<std::uint8_t> data);
  virtual ~VideoFrame() = default;

  VideoFrame(const VideoFrame&) = delete;
  VideoFrame& operator=(const VideoFrame&) = delete;
  // Hand-written rather than defaulted: a defaulted move copies the borrowed pointers and leaves
  // the source naming memory it no longer owns.
  VideoFrame(VideoFrame&&) noexcept;
  VideoFrame& operator=(VideoFrame&&) noexcept;

  /// Allocate a new frame with the correct buffer size for the given format.
  /// Data is zero-initialized.
  static VideoFrame create(int width, int height, VideoBufferType type);

  // Basic properties
  int width() const noexcept { return width_; }
  int height() const noexcept { return height_; }
  VideoBufferType type() const noexcept { return type_; }

  std::uint8_t* data() noexcept { return borrowed_data_ != nullptr ? borrowed_data_ : data_.data(); }
  const std::uint8_t* data() const noexcept { return borrowed_data_ != nullptr ? borrowed_data_ : data_.data(); }
  std::size_t dataSize() const noexcept { return borrowed_data_ != nullptr ? borrowed_size_ : data_.size(); }

  /// Whether this frame reads the SDK's buffer rather than owning a copy.
  /// See the class comment for what that means for pointer lifetimes.
  bool isBorrowed() const noexcept { return borrowed_data_ != nullptr; }

  /// Returns a borrowed frame's buffer to the SDK early, instead of waiting for the destructor.
  /// No-op on an owning frame, and safe to call twice.
  ///
  /// After this, data() and planeInfos() describe an empty frame. Anything still pointing into the
  /// old buffer is dangling.
  void release() noexcept;

  /// Compute plane layout for this frame (Y/U/V, UV, etc.), in terms of
  /// pointers & sizes relative to this frame's backing buffer.
  ///
  /// For packed formats (ARGB, RGB24) this will be either 1 plane or empty.
  ///
  /// A borrowed frame reports the layout the SDK gave it, rather than one derived from
  /// width/height, so a padded stride is described accurately.
  std::vector<VideoPlaneInfo> planeInfos() const;

  /// Convert this frame into another pixel format.
  ///
  /// This uses the underlying FFI `video_convert` pipeline to transform the
  /// current frame into a new `VideoFrame` with the requested
  /// `dst` buffer type (e.g. ARGB → I420, BGRA → RGB24, etc.).
  ///
  /// @param dst     Desired output format (see VideoBufferType).
  /// @param flip_y  If true, the converted frame will be vertically flipped.
  ///
  /// @return A new VideoFrame containing the converted image data.
  ///
  /// Notes:
  ///  - This function allocates a new buffer and copies pixel data; it does
  ///    not modify the original frame.
  ///  - This function performs a full CPU-based pixel conversion**. Depending
  ///    on resolution and format, this may involve substantial computation
  ///    (e.g., color-space transforms, planar repacking, vertical flipping).
  ///    Avoid calling this inside tight real-time loops unless necessary.
  ///  - Throws std::runtime_error if the FFI conversion fails or if the
  ///    format combination is unsupported.
  ///
  /// Typical usage:
  ///        VideoFrame i420 = frame.convert(VideoBufferType::I420);
  VideoFrame convert(VideoBufferType dst, bool flip_y = false) const;

protected:
  friend class VideoStream;
  // Only internal classes (e.g., VideoStream)
  // should construct frames directly from FFI buffers.

  /// Copies the pixels out of the SDK buffer and releases the handle.
  static VideoFrame fromOwnedInfo(const proto::OwnedVideoBuffer& owned);

  /// Wraps the SDK buffer in place and takes ownership of the handle, so the buffer lives exactly
  /// as long as the returned frame. No copy.
  static VideoFrame borrowOwnedInfo(const proto::OwnedVideoBuffer& owned);

private:
  int width_;
  int height_;
  VideoBufferType type_;

  /// Pixels for an owning frame; empty for a borrowed one.
  std::vector<std::uint8_t> data_;

  /// Non-null only for a borrowed frame, in which case it points into the buffer
  /// `borrowed_handle_` keeps alive. Doubles as the "is borrowed" flag.
  std::uint8_t* borrowed_data_{nullptr};
  std::size_t borrowed_size_{0};
  std::vector<VideoPlaneInfo> borrowed_planes_;
  FfiHandle borrowed_handle_;
};

} // namespace livekit
