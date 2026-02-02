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

#include <cstdint>
#include <string>
#include <vector>

namespace livekit {

namespace proto {
class AudioFrameBufferInfo;
class OwnedAudioFrameBuffer;
} // namespace proto

/**
 * @brief Represents a raw PCM audio frame with interleaved int16 samples.
 *
 * AudioFrame holds decoded audio data along with metadata such as sample rate,
 * number of channels, and samples per channel. It is used for capturing and
 * processing audio in the LiveKit SDK.
 */
class AudioFrame {
public:
  /**
   * Construct an AudioFrame from raw PCM samples.
   *
   * @param data                Interleaved PCM samples (int16).
   * @param sample_rate         Sample rate (Hz).
   * @param num_channels        Number of channels.
   * @param samples_per_channel Number of samples per channel.
   *
   * Throws std::invalid_argument if the data size is inconsistent with
   * num_channels * samples_per_channel.
   */
  AudioFrame(std::vector<std::int16_t> data, int sample_rate, int num_channels,
             int samples_per_channel);
  AudioFrame(); // Default constructor
  virtual ~AudioFrame() = default;

  /**
   * Create a new zero-initialized AudioFrame instance.
   */
  static AudioFrame create(int sample_rate, int num_channels,
                           int samples_per_channel);

  /**
   * Construct an AudioFrame by copying data out of an OwnedAudioFrameBuffer.
   */
  static AudioFrame fromOwnedInfo(const proto::OwnedAudioFrameBuffer &owned);

  // ---- Accessors ----

  const std::vector<std::int16_t> &data() const noexcept { return data_; }
  std::vector<std::int16_t> &data() noexcept { return data_; }

  /// Number of samples in the buffer (per all channels).
  std::size_t total_samples() const noexcept { return data_.size(); }

  /// Sample rate in Hz.
  int sample_rate() const noexcept { return sample_rate_; }

  /// Number of channels.
  int num_channels() const noexcept { return num_channels_; }

  /// Samples per channel.
  int samples_per_channel() const noexcept { return samples_per_channel_; }

  /// Duration in seconds (samples_per_channel / sample_rate).
  double duration() const noexcept;

  /// A human-readable description.
  std::string to_string() const;

protected:
  // Build a proto AudioFrameBufferInfo pointing at this frame’s data.
  // Used internally by AudioSource.
  proto::AudioFrameBufferInfo toProto() const;
  friend class AudioSource;

private:
  std::vector<std::int16_t> data_;
  int sample_rate_;
  int num_channels_;
  int samples_per_channel_;
};

} // namespace livekit