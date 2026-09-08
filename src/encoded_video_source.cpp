/*
 * Copyright 2026 LiveKit
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

#include "livekit/encoded_video_source.h"

#include <cstddef>
#include <stdexcept>

#include "ffi.pb.h"
#include "ffi_client.h"
#include "video_frame.pb.h"
#include "video_utils.h"

namespace livekit {
namespace {

constexpr std::size_t kMaxFrameSize = std::size_t{64} * 1024U * 1024U;
constexpr std::uint32_t kMaxFrameDimension = 65535U;

int validateDimension(int dimension) {
  if (dimension <= 0 || static_cast<std::uint32_t>(dimension) > kMaxFrameDimension) {
    throw std::invalid_argument("EncodedVideoSource: dimensions must be positive and at most 65535");
  }
  return dimension;
}

proto::VideoCodec toProtoCodec(VideoCodec codec) {
  switch (codec) {
    case VideoCodec::H264:
      return proto::VideoCodec::H264;
    case VideoCodec::H265:
      return proto::VideoCodec::H265;
    case VideoCodec::VP8:
      return proto::VideoCodec::VP8;
    case VideoCodec::VP9:
      return proto::VideoCodec::VP9;
    case VideoCodec::AV1:
      return proto::VideoCodec::AV1;
  }
  throw std::invalid_argument("EncodedVideoSource: unknown codec");
}

} // namespace

EncodedVideoSource::EncodedVideoSource(VideoCodec codec, int width, int height)
    : VideoSource(validateDimension(width), validateDimension(height), VideoSource::SourceType::Encoded),
      codec_(codec) {}

bool EncodedVideoSource::captureFrame(const Frame& frame) const {
  if ((frame.width == 0) != (frame.height == 0)) {
    throw std::invalid_argument("EncodedVideoSource: frame dimensions must both be zero or both be non-zero");
  }
  if (frame.width > kMaxFrameDimension || frame.height > kMaxFrameDimension) {
    throw std::invalid_argument("EncodedVideoSource: frame dimensions must not exceed 65535");
  }
  if (frame.data.empty() || frame.data.size() > kMaxFrameSize) {
    throw std::invalid_argument("EncodedVideoSource: frame payload must be between 1 byte and 64 MiB");
  }
  if (ffiHandleId() == 0) {
    throw std::runtime_error("EncodedVideoSource: invalid FFI handle");
  }

  proto::FfiRequest req;
  auto* msg = req.mutable_capture_encoded_video_frame();
  msg->set_source_handle(ffiHandleId());
  auto* buffer = msg->mutable_buffer();
  buffer->set_data_ptr(reinterpret_cast<std::uintptr_t>(frame.data.data()));
  buffer->set_data_len(frame.data.size());
  msg->set_codec(toProtoCodec(codec_));
  msg->set_frame_type(frame.is_keyframe ? proto::EncodedFrameType::ENCODED_FRAME_KEY
                                        : proto::EncodedFrameType::ENCODED_FRAME_DELTA);
  msg->set_width(frame.width == 0 ? static_cast<std::uint32_t>(width()) : frame.width);
  msg->set_height(frame.height == 0 ? static_cast<std::uint32_t>(height()) : frame.height);
  msg->set_timestamp_us(frame.timestamp_us);
  if (auto metadata = toProto(frame.metadata)) {
    msg->mutable_metadata()->CopyFrom(*metadata);
  }

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_capture_encoded_video_frame()) {
    throw std::runtime_error("EncodedVideoSource: missing capture response");
  }
  return resp.capture_encoded_video_frame().accepted();
}

EncodedVideoSource::Feedback EncodedVideoSource::takeFeedback() const {
  if (ffiHandleId() == 0) {
    throw std::runtime_error("EncodedVideoSource: invalid FFI handle");
  }

  proto::FfiRequest req;
  req.mutable_take_encoded_video_source_feedback()->set_source_handle(ffiHandleId());
  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_take_encoded_video_source_feedback()) {
    throw std::runtime_error("EncodedVideoSource: missing feedback response");
  }

  const auto& proto_feedback = resp.take_encoded_video_source_feedback();
  Feedback feedback;
  feedback.keyframe_requested = proto_feedback.keyframe_requested();
  if (proto_feedback.has_rate_control()) {
    feedback.rate_control =
        RateControl{proto_feedback.rate_control().target_bitrate_bps(), proto_feedback.rate_control().framerate_fps()};
  }
  return feedback;
}

} // namespace livekit
