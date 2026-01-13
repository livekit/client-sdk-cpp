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

#include "livekit/video_frame.h"

#include <cstring>
#include <stdexcept>
#include <vector>

#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/ffi_handle.h"
#include "video_frame.pb.h"

namespace livekit {

proto::VideoBufferType toProto(VideoBufferType t) {
  switch (t) {
  case VideoBufferType::ARGB:
    return proto::VideoBufferType::ARGB;
  case VideoBufferType::ABGR:
    return proto::VideoBufferType::ABGR;
  case VideoBufferType::RGBA:
    return proto::VideoBufferType::RGBA;
  case VideoBufferType::BGRA:
    return proto::VideoBufferType::BGRA;
  case VideoBufferType::RGB24:
    return proto::VideoBufferType::RGB24;
  case VideoBufferType::I420:
    return proto::VideoBufferType::I420;
  case VideoBufferType::I420A:
    return proto::VideoBufferType::I420A;
  case VideoBufferType::I422:
    return proto::VideoBufferType::I422;
  case VideoBufferType::I444:
    return proto::VideoBufferType::I444;
  case VideoBufferType::I010:
    return proto::VideoBufferType::I010;
  case VideoBufferType::NV12:
    return proto::VideoBufferType::NV12;
  default:
    throw std::runtime_error("Unknown VideoBufferType in toProto");
  }
}

// Map proto enum -> SDK enum
VideoBufferType fromProto(proto::VideoBufferType t) {
  switch (t) {
  case proto::VideoBufferType::ARGB:
    return VideoBufferType::ARGB;
  case proto::VideoBufferType::ABGR:
    return VideoBufferType::ABGR;
  case proto::VideoBufferType::RGBA:
    return VideoBufferType::RGBA;
  case proto::VideoBufferType::BGRA:
    return VideoBufferType::BGRA;
  case proto::VideoBufferType::RGB24:
    return VideoBufferType::RGB24;
  case proto::VideoBufferType::I420:
    return VideoBufferType::I420;
  case proto::VideoBufferType::I420A:
    return VideoBufferType::I420A;
  case proto::VideoBufferType::I422:
    return VideoBufferType::I422;
  case proto::VideoBufferType::I444:
    return VideoBufferType::I444;
  case proto::VideoBufferType::I010:
    return VideoBufferType::I010;
  case proto::VideoBufferType::NV12:
    return VideoBufferType::NV12;
  default:
    throw std::runtime_error("Unknown proto::VideoBufferType in fromProto");
  }
}

proto::VideoBufferInfo toProto(const VideoFrame &frame) {
  proto::VideoBufferInfo info;

  const int w = frame.width();
  const int h = frame.height();
  info.set_width(w);
  info.set_height(h);
  info.set_type(toProto(frame.type()));

  // Backing data pointer for the whole buffer
  auto base_ptr = reinterpret_cast<std::uint64_t>(frame.data());
  info.set_data_ptr(base_ptr);

  // Compute plane layout for the current format
  auto planes = frame.planeInfos();
  for (const auto &plane : planes) {
    auto *cmpt = info.add_components();
    cmpt->set_data_ptr(static_cast<std::uint64_t>(plane.data_ptr));
    cmpt->set_stride(plane.stride);
    cmpt->set_size(plane.size);
  }

  // Stride for main packed formats.
  std::uint32_t stride = 0;
  switch (frame.type()) {
  case VideoBufferType::ARGB:
  case VideoBufferType::ABGR:
  case VideoBufferType::RGBA:
  case VideoBufferType::BGRA:
    stride = static_cast<std::uint32_t>(w) * 4;
    break;
  case VideoBufferType::RGB24:
    stride = static_cast<std::uint32_t>(w) * 3;
    break;
  default:
    stride = 0; // not used / unknown for planar formats
    break;
  }
  info.set_stride(stride);
  return info;
}

VideoFrame fromOwnedProto(const proto::OwnedVideoBuffer &owned) {
  const auto &info = owned.info();

  const int width = static_cast<int>(info.width());
  const int height = static_cast<int>(info.height());
  const VideoBufferType type = fromProto(info.type());

  // Allocate a new VideoFrame with the correct size/format
  VideoFrame frame = VideoFrame::create(width, height, type);

  // Copy from the FFI-provided buffer into our own backing storage
  auto *dst = frame.data();
  const std::size_t dst_size = frame.dataSize();

  const auto src_ptr = info.data_ptr();
  if (src_ptr == 0) {
    throw std::runtime_error("fromOwnedProto: info.data_ptr is null");
  }
  const auto *src = reinterpret_cast<const std::uint8_t *>(src_ptr);

  std::memcpy(dst, src, dst_size);

  // Drop the owned FFI handle to let the core free its side.
  {
    FfiHandle tmp(owned.handle().id());
    // tmp destructor will dispose the handle via FFI.
  }

  return frame;
}

VideoFrame convertViaFfi(const VideoFrame &frame, VideoBufferType dst,
                           bool flip_y) {
  proto::FfiRequest req;
  auto *vc = req.mutable_video_convert();
  vc->set_flip_y(flip_y);
  vc->set_dst_type(toProto(dst));
  vc->mutable_buffer()->CopyFrom(toProto(frame));

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_video_convert()) {
    throw std::runtime_error(
        "convertViaFfi: FfiResponse missing video_convert");
  }
  const auto &vc_resp = resp.video_convert();
  if (!vc_resp.error().empty()) {
    throw std::runtime_error("convertViaFfi: " + vc_resp.error());
  }
  // vc_resp.buffer() is an OwnedVideoBuffer
  return fromOwnedProto(vc_resp.buffer());
}

} // namespace livekit
