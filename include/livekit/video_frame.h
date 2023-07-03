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

#include "livekit/ffi_client.h"

#include "video_frame.pb.h"

namespace livekit {
    class I420Buffer;

    /**
     * Mainly used to simplify the usage of to_argb method
     * So the users don't need to deal with ctypes
    */
    struct ArgbFrame {
        ArgbFrame(VideoFormatType format, int width, int height) : format(format), width(width), height(height) {
            int size = width * height * sizeof(uint32_t);
            data = new uint8_t[size];
        }

        VideoFormatType format;
        int width;
        int height;
        uint8_t* data;

        I420Buffer ToI420();
    };

    class VideoFrameBuffer {
    public:
        VideoFrameBuffer(FfiHandle&& ffiHandle, VideoFrameBufferInfo&& info) : ffiHandle_(std::move(ffiHandle)), info_(std::move(info)) {}
        VideoFrameBuffer(const FfiHandle& ffiHandle, const VideoFrameBufferInfo& info) : ffiHandle_(ffiHandle), info_(info) {}

        const FfiHandle& GetHandle() const {
            return ffiHandle_;
        }

        int GetWidth() {
            return info_.width();
        }

        int GetHeight() {
            return info_.height();
        }

        VideoFrameBufferType GetType() {
            return info_.buffer_type();
        }

        I420Buffer ToI420();
        void ToArgb(const ArgbFrame& dst);

        static VideoFrameBuffer Create(FfiHandle&& ffi_handle, VideoFrameBufferInfo&& info);

    protected:
        FfiHandle ffiHandle_;
        VideoFrameBufferInfo info_;
    };

    class PlanarYuvBuffer : public VideoFrameBuffer {
    public:
        PlanarYuvBuffer(FfiHandle&& ffiHandle, VideoFrameBufferInfo&& info) : VideoFrameBuffer(std::move(ffiHandle), std::move(info)) {}
        int GetChromaWidth() const {
            return info_.yuv().chroma_width();
        }

        int GetChromaHeight() const {
            return info_.yuv().chroma_height();
        }

        int GetStrideY() const {
            return info_.yuv().stride_y();
        }

        int GetStrideU() const {
            return info_.yuv().stride_u();
        }

        int GetStrideV() const {
            return info_.yuv().stride_v();
        }
    };

    class PlanarYuv8Buffer : public PlanarYuvBuffer {
    public:
        PlanarYuv8Buffer(FfiHandle&& ffiHandle, VideoFrameBufferInfo&& info) : PlanarYuvBuffer(std::move(ffiHandle), std::move(info)) {}
        uint8_t* GetDataY() const {
            reinterpret_cast<uint8_t*>(info_.yuv().data_y_ptr());
        }

        uint8_t* GetDataU() const {
            reinterpret_cast<uint8_t*>(info_.yuv().data_u_ptr());
        }

        uint8_t* GetDataV() const {
            reinterpret_cast<uint8_t*>(info_.yuv().data_v_ptr());
        }
    };

    class I420Buffer : public PlanarYuv8Buffer{
    public:
        I420Buffer(FfiHandle&& ffiHandle, VideoFrameBufferInfo&& info) : PlanarYuv8Buffer(std::move(ffiHandle), std::move(info)) {}
    };

    class VideoFrame
    {
    public:
        VideoFrame(int64_t timestampUs, VideoRotation rotation, const VideoFrameBuffer&& buffer) :
            timestampUs_(timestampUs), rotation_(rotation), buffer_(buffer) {}

        const VideoFrameBuffer& GetBuffer() const { return buffer_; }
        const VideoRotation& GetRotation() const { return rotation_; }
        const int64_t GetTimestamp() const { return timestampUs_; }

    private:
        VideoFrameBuffer buffer_;
        int64_t timestampUs_;
        VideoRotation rotation_;
    };
}

#endif /* LIVEKIT_VIDEO_FRAME_H */
