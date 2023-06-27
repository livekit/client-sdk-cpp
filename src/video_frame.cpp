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

#include "livekit/video_frame.h"

#include "ffi.pb.h"
#include "video_frame.pb.h"

namespace livekit
{
   I420Buffer ArgbFrame::ToI420() {
        FFIRequest request;
        auto i420Argb = request.mutable_to_i420()->mutable_argb();
        i420Argb->set_format(format);
        i420Argb->set_width(width);
        i420Argb->set_height(height);
        i420Argb->set_stride(width * 4);
        i420Argb->set_ptr(reinterpret_cast<uint64_t>(data));

        FFIResponse response = FfiClient::getInstance().SendRequest(request);
        VideoFrameBufferInfo bufferInfo(std::move(response.to_i420().buffer()));
        FfiHandle ffiHandle(bufferInfo.handle().id());

        return I420Buffer(std::move(ffiHandle), std::move(bufferInfo));
   }

    // I420Buffer VideoFrameBuffer::ToI420() {
    //     FFIRequest request;
    //     request.to_i420.buffer.set_id(ffiHandle_.handle());

    //     FFIResponse response = FfiClient::getInstance().SendReques(request);

    //     VideoFrameBufferInfo new_info = response.to_i420().buffer();
    //     FfiHandle ffi_handle(new_info.handle().id());
    //     return I420Buffer(ffi_handle, new_info);
    // }

    // void ToArgb(const ArgbFrame& dst) {
    //     FFIRequest request;
    //     request.to_argb.buffer.set_id(ffiHandle_.handle());
    //     request.to_argb.dst_ptr = dst.data;
    //     request.to_argb.dst_format = dst.format;
    //     request.to_argb.dst_stride = dst.width * 4;
    //     request.to_argb.dst_width = dst.width;
    //     request.to_argb.dst_height = dst.height;

    //     FfiClient::getInstance().SendRequest(request);
    // }

    I420Buffer VideoFrameBuffer::ToI420() {
        FFIRequest request;
        request.mutable_to_i420()->mutable_buffer()->set_id(ffiHandle_.handle);

        FFIResponse response = FfiClient::getInstance().SendRequest(request);

        VideoFrameBufferInfo newInfo = response.to_i420().buffer();
        FfiHandle ffiHandle(newInfo.handle().id());
        return I420Buffer(std::move(ffiHandle), std::move(newInfo));
    }

    void VideoFrameBuffer::ToArgb(const ArgbFrame& dst) {
        FFIRequest request;
        ToARGBRequest* const argb = request.mutable_to_argb();
        argb->mutable_buffer()->set_id(ffiHandle_.handle);
        argb->set_dst_ptr(reinterpret_cast<uint64_t>(dst.data));
        argb->set_dst_format(dst.format);
        argb->set_dst_stride(dst.width * 4);
        argb->set_dst_width(dst.width);
        argb->set_dst_height(dst.height);

        FfiClient::getInstance().SendRequest(request);
    }

    VideoFrameBuffer VideoFrameBuffer::Create(FfiHandle&& ffiHandle, VideoFrameBufferInfo&& info) {
        if (info.buffer_type() == VideoFrameBufferType::I420) {
            return I420Buffer(std::move(ffiHandle), std::move(info));
        } else {
            throw std::runtime_error("unsupported buffer type");
        }
        // if (info.buffer_type() == VideoFrameBufferType::NATIVE) {
        //     return NativeVideoFrameBuffer(ffiHandle, info);
        // } else if (info.buffer_type() == VideoFrameBufferType::I420) {
        //     return I420Buffer(ffiHandle, info);
        // } else if (info.buffer_type() == VideoFrameBufferType::I420A) {
        //     return I420ABuffer(ffiHandle, info);
        // } else if (info.buffer_type() == VideoFrameBufferType::I422) {
        //     return I422Buffer(ffiHandle, info);
        // } else if (info.buffer_type() == VideoFrameBufferType::I444) {
        //     return I444Buffer(ffiHandle, info);
        // } else if (info.buffer_type() == VideoFrameBufferType::I010) {
        //     return I010Buffer(ffiHandle, info);
        // } else if (info.buffer_type() == VideoFrameBufferType::NV12) {
        //     return NV12Buffer(ffiHandle, info);
        // }
    }
}
