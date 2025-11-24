
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

#include "livekit/livekit.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Simple WAV container for 16-bit PCM files
struct WavData {
  int sample_rate = 0;
  int num_channels = 0;
  std::vector<int16_t> samples;
};

// Helper that loads 16-bit PCM WAV (16-bit, PCM only)
WavData loadWav16(const std::string &path);

using namespace livekit;

class WavAudioSource {
public:
  // loop_enabled: whether to loop when reaching the end
  WavAudioSource(const std::string &path, int expected_sample_rate,
                 int expected_channels, bool loop_enabled = true);

  // Fill a frame with the next chunk of audio.
  // This does NOT call captureFrame(): you do that outside.
  void fillFrame(AudioFrame &frame);

private:
  void initLoopDelayCounter();

  WavData wav_;
  std::size_t playhead_ = 0;

  const bool loop_enabled_;
  int sample_rate_;
  int num_channels_;
};
