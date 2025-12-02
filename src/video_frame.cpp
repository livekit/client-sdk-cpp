#include "livekit/video_frame.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "livekit/ffi_handle.h"
#include "video_utils.h"

namespace livekit {
namespace {

// Compute total buffer size in bytes for (width, height, type).
std::size_t computeBufferSize(int width, int height, VideoBufferType type) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument(
        "LKVideoFrame: width and height must be positive");
  }

  const auto w = static_cast<std::size_t>(width);
  const auto h = static_cast<std::size_t>(height);
  switch (type) {
  case VideoBufferType::ARGB:
  case VideoBufferType::ABGR:
  case VideoBufferType::RGBA:
  case VideoBufferType::BGRA:
    // 4 bytes per pixel
    return w * h * 4;

  case VideoBufferType::RGB24:
    // 3 bytes per pixel
    return w * h * 3;

  case VideoBufferType::I444:
    // Y, U, V all full resolution
    return w * h * 3;

  case VideoBufferType::I420:
  case VideoBufferType::NV12:
  case VideoBufferType::I010: {
    // Y full, U and V subsampled 2x2
    const std::size_t chroma_w = (w + 1) / 2;
    const std::size_t chroma_h = (h + 1) / 2;
    if (type == VideoBufferType::I420) {
      // Y (1 byte) + U (1 byte) + V (1 byte)
      return w * h + chroma_w * chroma_h * 2;
    } else if (type == VideoBufferType::NV12) {
      // Y (1 byte), UV interleaved (2 bytes per chroma sample)
      return w * h + chroma_w * chroma_h * 2;
    } else { // I010, 16 bits per sample in memory
      // Y: 2 bytes per sample, U & V: 2 bytes per sample
      return w * h * 2 + chroma_w * chroma_h * 4;
    }
  }

  case VideoBufferType::I420A: {
    // Y full, U & V 2x2, plus alpha full res
    const std::size_t chroma_w = (w + 1) / 2;
    const std::size_t chroma_h = (h + 1) / 2;
    // Y + A are full resolution, U + V subsampled
    return w * h * 2 + chroma_w * chroma_h * 2;
  }

  case VideoBufferType::I422: {
    // Y full, U & V subsampled horizontally only
    const std::size_t chroma_w = (w + 1) / 2;
    return w * h + chroma_w * h * 2;
  }

  default:
    throw std::runtime_error("LKVideoFrame: unsupported VideoBufferType");
  }
}

// Compute plane layout for (base_ptr, width, height, type)
std::vector<VideoPlaneInfo>
computePlaneInfos(uintptr_t base, int width, int height, VideoBufferType type) {
  std::vector<VideoPlaneInfo> planes;
  if (!base || width <= 0 || height <= 0) {
    std::cerr << "[LKVideoFrame] Warning: invalid planeInfos input (ptr="
              << base << ", w=" << width << ", h=" << height << ")\n";
    return planes;
  }
  const auto w = static_cast<uint32_t>(width);
  const auto h = static_cast<uint32_t>(height);
  auto pushPlane = [&](uintptr_t ptr, uint32_t stride, uint32_t size) {
    VideoPlaneInfo info;
    info.data_ptr = ptr;
    info.stride = stride;
    info.size = size;
    planes.push_back(info);
  };

  switch (type) {
  case VideoBufferType::ARGB:
  case VideoBufferType::ABGR:
  case VideoBufferType::RGBA:
  case VideoBufferType::BGRA: {
    const uint32_t stride = w * 4;
    const uint32_t size = stride * h;
    pushPlane(base, stride, size);
    break;
  }

  case VideoBufferType::RGB24: {
    const uint32_t stride = w * 3;
    const uint32_t size = stride * h;
    pushPlane(base, stride, size);
    break;
  }

  case VideoBufferType::I420: {
    const uint32_t chroma_w = (w + 1) / 2;
    const uint32_t chroma_h = (h + 1) / 2;

    // Y
    const uint32_t y_stride = w;
    const uint32_t y_size = w * h;
    uintptr_t y_ptr = base;
    pushPlane(y_ptr, y_stride, y_size);

    // U
    const uint32_t u_stride = chroma_w;
    const uint32_t u_size = chroma_w * chroma_h;
    uintptr_t u_ptr = y_ptr + y_size;
    pushPlane(u_ptr, u_stride, u_size);

    // V
    const uint32_t v_stride = chroma_w;
    const uint32_t v_size = chroma_w * chroma_h;
    uintptr_t v_ptr = u_ptr + u_size;
    pushPlane(v_ptr, v_stride, v_size);
    break;
  }

  case VideoBufferType::I420A: {
    const uint32_t chroma_w = (w + 1) / 2;
    const uint32_t chroma_h = (h + 1) / 2;

    // Y
    const uint32_t y_stride = w;
    const uint32_t y_size = w * h;
    uintptr_t y_ptr = base;
    pushPlane(y_ptr, y_stride, y_size);

    // U
    const uint32_t u_stride = chroma_w;
    const uint32_t u_size = chroma_w * chroma_h;
    uintptr_t u_ptr = y_ptr + y_size;
    pushPlane(u_ptr, u_stride, u_size);

    // V
    const uint32_t v_stride = chroma_w;
    const uint32_t v_size = chroma_w * chroma_h;
    uintptr_t v_ptr = u_ptr + u_size;
    pushPlane(v_ptr, v_stride, v_size);

    // A (full res)
    const uint32_t a_stride = w;
    const uint32_t a_size = w * h;
    uintptr_t a_ptr = v_ptr + v_size;
    pushPlane(a_ptr, a_stride, a_size);
    break;
  }

  case VideoBufferType::I422: {
    const uint32_t chroma_w = (w + 1) / 2;

    // Y
    const uint32_t y_stride = w;
    const uint32_t y_size = w * h;
    uintptr_t y_ptr = base;
    pushPlane(y_ptr, y_stride, y_size);

    // U
    const uint32_t u_stride = chroma_w;
    const uint32_t u_size = chroma_w * h;
    uintptr_t u_ptr = y_ptr + y_size;
    pushPlane(u_ptr, u_stride, u_size);

    // V
    const uint32_t v_stride = chroma_w;
    const uint32_t v_size = chroma_w * h;
    uintptr_t v_ptr = u_ptr + u_size;
    pushPlane(v_ptr, v_stride, v_size);
    break;
  }

  case VideoBufferType::I444: {
    // All planes full-res
    const uint32_t y_stride = w;
    const uint32_t y_size = w * h;
    uintptr_t y_ptr = base;
    pushPlane(y_ptr, y_stride, y_size);

    const uint32_t u_stride = w;
    const uint32_t u_size = w * h;
    uintptr_t u_ptr = y_ptr + y_size;
    pushPlane(u_ptr, u_stride, u_size);

    const uint32_t v_stride = w;
    const uint32_t v_size = w * h;
    uintptr_t v_ptr = u_ptr + u_size;
    pushPlane(v_ptr, v_stride, v_size);
    break;
  }

  case VideoBufferType::I010: {
    // 16-bit per sample
    const uint32_t chroma_w = (w + 1) / 2;
    const uint32_t chroma_h = (h + 1) / 2;

    // Y
    const uint32_t y_stride = w * 2;
    const uint32_t y_size = w * h * 2;
    uintptr_t y_ptr = base;
    pushPlane(y_ptr, y_stride, y_size);

    // U
    const uint32_t u_stride = chroma_w * 2;
    const uint32_t u_size = chroma_w * chroma_h * 2;
    uintptr_t u_ptr = y_ptr + y_size;
    pushPlane(u_ptr, u_stride, u_size);

    // V
    const uint32_t v_stride = chroma_w * 2;
    const uint32_t v_size = chroma_w * chroma_h * 2;
    uintptr_t v_ptr = u_ptr + u_size;
    pushPlane(v_ptr, v_stride, v_size);
    break;
  }

  case VideoBufferType::NV12: {
    const uint32_t chroma_w = (w + 1) / 2;
    const uint32_t chroma_h = (h + 1) / 2;

    // Y
    const uint32_t y_stride = w;
    const uint32_t y_size = w * h;
    uintptr_t y_ptr = base;
    pushPlane(y_ptr, y_stride, y_size);

    // UV interleaved
    const uint32_t uv_stride = chroma_w * 2;
    const uint32_t uv_size = chroma_w * chroma_h * 2;
    uintptr_t uv_ptr = y_ptr + y_size;
    pushPlane(uv_ptr, uv_stride, uv_size);
    break;
  }

  default:
    // Unknown or unsupported -> no planes
    break;
  }

  return planes;
}

} // namespace

// ----------------------------------------------------------------------------
// LKVideoFrame implementation
// ----------------------------------------------------------------------------

LKVideoFrame::LKVideoFrame()
    : width_{0}, height_{0}, type_{VideoBufferType::BGRA}, data_{} {}

LKVideoFrame::LKVideoFrame(int width, int height, VideoBufferType type,
                           std::vector<std::uint8_t> data)
    : width_(width), height_(height), type_(type), data_(std::move(data)) {
  const std::size_t expected = computeBufferSize(width_, height_, type_);
  if (data_.size() < expected) {
    throw std::invalid_argument("LKVideoFrame: provided data is too small for "
                                "the specified format and size");
  }
}

LKVideoFrame LKVideoFrame::create(int width, int height, VideoBufferType type) {
  const std::size_t size = computeBufferSize(width, height, type);
  std::vector<std::uint8_t> buffer(size, 0);
  return LKVideoFrame(width, height, type, std::move(buffer));
}

std::vector<VideoPlaneInfo> LKVideoFrame::planeInfos() const {
  if (data_.empty()) {
    return {};
  }

  uintptr_t base = reinterpret_cast<uintptr_t>(data_.data());
  return computePlaneInfos(base, width_, height_, type_);
}

LKVideoFrame LKVideoFrame::convert(VideoBufferType dst, bool flip_y) const {
  // Fast path: same format, no flip -> just clone the buffer.
  // We still return a *new* LKVideoFrame, never `*this`, so copy-ctor
  // being deleted is not a problem.
  if (dst == type_ && !flip_y) {
    std::cerr << "KVideoFrame::convert Warning: converting to the same format"
              << std::endl;
    // copy pixel data
    std::vector<std::uint8_t> buf = data_;
    return LKVideoFrame(width_, height_, type_, std::move(buf));
  }

  // General path: delegate to the FFI-based conversion helper.
  // This returns a brand new LKVideoFrame (move-constructed / elided).
  return convertViaFfi(*this, dst, flip_y);
}

LKVideoFrame LKVideoFrame::fromOwnedInfo(const proto::OwnedVideoBuffer &owned) {
  const auto &info = owned.info();
  const int width = static_cast<int>(info.width());
  const int height = static_cast<int>(info.height());
  const VideoBufferType type = fromProto(info.type());

  std::vector<std::uint8_t> buffer;

  if (info.components_size() > 0) {
    // Multi-plane (e.g. I420, NV12, etc.). We pack planes back-to-back.
    std::size_t total_size = 0;
    for (const auto &comp : info.components()) {
      total_size += static_cast<std::size_t>(comp.size());
    }

    buffer.resize(total_size);
    std::size_t offset = 0;
    for (const auto &comp : info.components()) {
      const auto sz = static_cast<std::size_t>(comp.size());
      const auto src_ptr = reinterpret_cast<const std::uint8_t *>(
          static_cast<std::uintptr_t>(comp.data_ptr()));

      std::memcpy(buffer.data() + offset, src_ptr, sz);
      offset += sz;
    }
  } else {
    // Packed format: treat top-level data_ptr as a single contiguous buffer.
    const auto src_ptr = reinterpret_cast<const std::uint8_t *>(
        static_cast<std::uintptr_t>(info.data_ptr()));

    std::size_t total_size = 0;
    if (info.has_stride()) {
      // Use stride * height as total size (includes per-row padding if any).
      total_size = static_cast<std::size_t>(info.stride()) *
                   static_cast<std::size_t>(height);
    } else {
      // Use our generic buffer-size helper (width/height/type).
      total_size = computeBufferSize(width, height, type);
    }

    buffer.resize(total_size);
    std::memcpy(buffer.data(), src_ptr, total_size);
  }

  // Release the FFI-owned buffer after copying the data.
  {
    FfiHandle owned_handle(static_cast<std::uintptr_t>(owned.handle().id()));
    // owned_handle destroyed at end of scope → native buffer disposed.
  }

  return LKVideoFrame(width, height, type, std::move(buffer));
}

} // namespace livekit
