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

#include "livekit/audio_frame.h"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "audio_frame.pb.h"
#include "handle.pb.h"
#include "livekit/ffi_handle.h"

namespace livekit {

AudioFrame::AudioFrame(std::vector<std::int16_t> data, int sample_rate,
                       int num_channels, int samples_per_channel)
    : data_(std::move(data)), sample_rate_(sample_rate),
      num_channels_(num_channels), samples_per_channel_(samples_per_channel) {
  const std::size_t expected = static_cast<std::size_t>(num_channels_) *
                               static_cast<std::size_t>(samples_per_channel_);

  if (data_.size() < expected) {
    throw std::invalid_argument(
        "AudioFrame: data size must be >= num_channels * samples_per_channel");
  }
  if (data_.size() % expected != 0) {
    throw std::invalid_argument(
        "AudioFrame: data size must be an exact multiple of "
        "num_channels * samples_per_channel");
  }
}

AudioFrame AudioFrame::create(int sample_rate, int num_channels,
                              int samples_per_channel) {
  const std::size_t count = static_cast<std::size_t>(num_channels) *
                            static_cast<std::size_t>(samples_per_channel);
  std::vector<std::int16_t> data(count, 0);
  return AudioFrame(std::move(data), sample_rate, num_channels,
                    samples_per_channel);
}

AudioFrame
AudioFrame::fromOwnedInfo(const proto::OwnedAudioFrameBuffer &owned) {
  const auto &info = owned.info();

  const int num_channels = static_cast<int>(info.num_channels());
  const int samples_per_channel = static_cast<int>(info.samples_per_channel());
  const int sample_rate = static_cast<int>(info.sample_rate());

  const std::size_t count = static_cast<std::size_t>(num_channels) *
                            static_cast<std::size_t>(samples_per_channel);

  const std::int16_t *ptr =
      reinterpret_cast<const std::int16_t *>(info.data_ptr());

  if (ptr == nullptr && count > 0) {
    throw std::runtime_error(
        "AudioFrame::fromOwnedInfo: null data_ptr with nonzero size");
  }

  std::vector<std::int16_t> data;
  if (count > 0) {
    data.assign(ptr, ptr + count);
  }

  {
    FfiHandle guard(static_cast<uintptr_t>(owned.handle().id()));
    // guard is destroyed at end of scope, which should call into the FFI to
    // drop the OwnedAudioFrameBuffer.
  }

  return AudioFrame(std::move(data), sample_rate, num_channels,
                    samples_per_channel);
}

proto::AudioFrameBufferInfo AudioFrame::toProto() const {
  proto::AudioFrameBufferInfo info;
  const std::uint64_t ptr =
      data_.empty() ? 0 : reinterpret_cast<std::uint64_t>(data_.data());

  info.set_data_ptr(ptr);
  info.set_num_channels(static_cast<std::uint32_t>(num_channels_));
  info.set_sample_rate(static_cast<std::uint32_t>(sample_rate_));
  info.set_samples_per_channel(
      static_cast<std::uint32_t>(samples_per_channel_));
  return info;
}

double AudioFrame::duration() const noexcept {
  if (sample_rate_ <= 0) {
    return 0.0;
  }
  return static_cast<double>(samples_per_channel_) /
         static_cast<double>(sample_rate_);
}

std::string AudioFrame::to_string() const {
  std::ostringstream oss;
  oss << "rtc.AudioFrame(sample_rate=" << sample_rate_
      << ", num_channels=" << num_channels_
      << ", samples_per_channel=" << samples_per_channel_
      << ", duration=" << std::fixed << std::setprecision(3) << duration()
      << ")";
  return oss.str();
}

} // namespace livekit
