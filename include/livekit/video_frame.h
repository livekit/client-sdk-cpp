/*
 * Copyright 2023 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the “License”);
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

#ifndef LIVEKIT_VIDEO_FRAME_H
#define LIVEKIT_VIDEO_FRAME_H

#include <sys/types.h>

#include <cstddef>
#include <cstdint>

#include "livekit/ffi_client.h"

namespace livekit {
class I420Buffer;

struct ArgbFrame {
  ArgbFrame(proto::VideoFormatType format, int width, int height)
      : format(format), width(width), height(height) {
    std::cout << "ArgbFrame::ArgbFrame" << std::endl;
    size = width * height * sizeof(uint32_t);
    data = new uint8_t[size];
  }

  ~ArgbFrame() {
    std::cout << "ArgbFrame::~ArgbFrame" << std::endl;
    delete[] data;
  }

  proto::VideoFormatType format;
  int width, height;
  uint8_t* data;
  size_t size;

  I420Buffer ToI420();
};

class VideoFrameBuffer {
 public:
  VideoFrameBuffer(FfiHandle& ffiHandle, proto::VideoFrameBufferInfo& info)
      : handle_(std::move(ffiHandle)), info_(info) {}

  const FfiHandle& GetHandle() const { return handle_; }
  int GetWidth() { return info_.width(); }
  int GetHeight() { return info_.height(); }
  proto::VideoFrameBufferType GetType() { return info_.buffer_type(); }

  I420Buffer ToI420();
  void ToArgb(const ArgbFrame& dst);

  static VideoFrameBuffer Create(FfiHandle& ffi_handle,
                                 proto::VideoFrameBufferInfo& info);

 protected:
  FfiHandle handle_{INVALID_HANDLE};
  proto::VideoFrameBufferInfo info_;
};

class PlanarYuvBuffer : public VideoFrameBuffer {
 public:
  PlanarYuvBuffer(FfiHandle& ffiHandle, proto::VideoFrameBufferInfo& info)
      : VideoFrameBuffer(ffiHandle, info) {}

  int GetChromaWidth() const { return info_.yuv().chroma_width(); }
  int GetChromaHeight() const { return info_.yuv().chroma_height(); }
  int GetStrideY() const { return info_.yuv().stride_y(); }
  int GetStrideU() const { return info_.yuv().stride_u(); }
  int GetStrideV() const { return info_.yuv().stride_v(); }
};

class PlanarYuv8Buffer : public PlanarYuvBuffer {
 public:
  PlanarYuv8Buffer(FfiHandle& ffiHandle, proto::VideoFrameBufferInfo& info)
      : PlanarYuvBuffer(ffiHandle, info) {}

  uint8_t* GetDataY() const {
    return reinterpret_cast<uint8_t*>(info_.yuv().data_y_ptr());
  }

  uint8_t* GetDataU() const {
    return reinterpret_cast<uint8_t*>(info_.yuv().data_u_ptr());
  }

  uint8_t* GetDataV() const {
    return reinterpret_cast<uint8_t*>(info_.yuv().data_v_ptr());
  }
};

class I420Buffer : public PlanarYuv8Buffer {
 public:
  I420Buffer(FfiHandle& ffiHandle, proto::VideoFrameBufferInfo& info)
      : PlanarYuv8Buffer(ffiHandle, info) {}
};

class VideoFrame {
 public:
  VideoFrame(int64_t timestampUs,
             proto::VideoRotation rotation,
             VideoFrameBuffer buffer)
      : timestampUs_(timestampUs),
        rotation_(rotation),
        buffer_(std::move(buffer)) {}

  const VideoFrameBuffer& GetBuffer() const { return buffer_; }
  const proto::VideoRotation& GetRotation() const { return rotation_; }
  const int64_t GetTimestamp() const { return timestampUs_; }

 private:
  VideoFrameBuffer buffer_;
  int64_t timestampUs_;
  proto::VideoRotation rotation_;
};
}  // namespace livekit

#endif /* LIVEKIT_VIDEO_FRAME_H */
