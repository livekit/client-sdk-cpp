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

#include "wav_audio_source.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

// --------------------------------------------------
// Minimal WAV loader (16-bit PCM only)
// --------------------------------------------------
WavData load_wav16(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open WAV file: " + path);
  }

  auto read_u32 = [&](uint32_t &out_value) {
    file.read(reinterpret_cast<char *>(&out_value), 4);
  };
  auto read_u16 = [&](uint16_t &out_value) {
    file.read(reinterpret_cast<char *>(&out_value), 2);
  };

  char riff[4];
  file.read(riff, 4);
  if (std::strncmp(riff, "RIFF", 4) != 0) {
    throw std::runtime_error("Not a RIFF file");
  }

  uint32_t chunk_size = 0;
  read_u32(chunk_size);

  char wave[4];
  file.read(wave, 4);
  if (std::strncmp(wave, "WAVE", 4) != 0) {
    throw std::runtime_error("Not a WAVE file");
  }

  uint16_t audio_format = 0;
  uint16_t num_channels = 0;
  uint32_t sample_rate = 0;
  uint16_t bits_per_sample = 0;

  bool have_fmt = false;
  bool have_data = false;
  std::vector<int16_t> samples;

  while (!have_data && file) {
    char sub_id[4];
    file.read(sub_id, 4);

    uint32_t sub_size = 0;
    read_u32(sub_size);

    if (std::strncmp(sub_id, "fmt ", 4) == 0) {
      have_fmt = true;

      read_u16(audio_format);
      read_u16(num_channels);
      read_u32(sample_rate);

      uint32_t byte_rate = 0;
      uint16_t block_align = 0;
      read_u32(byte_rate);
      read_u16(block_align);
      read_u16(bits_per_sample);

      if (sub_size > 16) {
        file.seekg(sub_size - 16, std::ios::cur);
      }

      if (audio_format != 1) {
        throw std::runtime_error("Only PCM WAV supported");
      }
      if (bits_per_sample != 16) {
        throw std::runtime_error("Only 16-bit WAV supported");
      }

    } else if (std::strncmp(sub_id, "data", 4) == 0) {
      if (!have_fmt) {
        throw std::runtime_error("data chunk appeared before fmt chunk");
      }

      have_data = true;
      const std::size_t count = sub_size / sizeof(int16_t);
      samples.resize(count);
      file.read(reinterpret_cast<char *>(samples.data()), sub_size);

    } else {
      // Unknown chunk: skip it
      file.seekg(sub_size, std::ios::cur);
    }
  }

  if (!have_data) {
    throw std::runtime_error("No data chunk in WAV file");
  }

  WavData out;
  out.sample_rate = static_cast<int>(sample_rate);
  out.num_channels = static_cast<int>(num_channels);
  out.samples = std::move(samples);
  return out;
}

WavAudioSource::WavAudioSource(const std::string &path,
                               int expected_sample_rate, int expected_channels,
                               bool loop_enabled)
    : loop_enabled_(loop_enabled) {
  wav_ = load_wav16(path);

  if (wav_.sample_rate != expected_sample_rate) {
    throw std::runtime_error("WAV sample rate mismatch");
  }
  if (wav_.num_channels != expected_channels) {
    throw std::runtime_error("WAV channel count mismatch");
  }

  sample_rate_ = wav_.sample_rate;
  num_channels_ = wav_.num_channels;

  playhead_ = 0;
}

void WavAudioSource::fillFrame(AudioFrame &frame) {
  const std::size_t frame_samples =
      static_cast<std::size_t>(frame.num_channels()) *
      static_cast<std::size_t>(frame.samples_per_channel());

  int16_t *dst = frame.data().data();
  const std::size_t total_wav_samples = wav_.samples.size();

  for (std::size_t i = 0; i < frame_samples; ++i) {
    if (playhead_ < total_wav_samples) {
      dst[i] = wav_.samples[playhead_];
      ++playhead_;
    } else if (loop_enabled_ && total_wav_samples > 0) {
      playhead_ = 0;
      dst[i] = wav_.samples[playhead_];
      ++playhead_;
    } else {
      dst[i] = 0;
    }
  }
}
