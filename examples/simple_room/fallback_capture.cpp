/*
 * Copyright 2025 LiveKit, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fallback_capture.h"

#include <array>
#include <chrono>
#include <thread>

#include "livekit/livekit.h"
#include "wav_audio_source.h"

using namespace livekit;

// Test utils to run a capture loop to publish noisy audio frames to the room
void runNoiseCaptureLoop(const std::shared_ptr<AudioSource> &source,
                         std::atomic<bool> &running_flag) {
  const int sample_rate = source->sample_rate();
  const int num_channels = source->num_channels();
  const int frame_ms = 10;
  const int samples_per_channel = sample_rate * frame_ms / 1000;

  // FIX: variable name should not shadow the type
  WavAudioSource wavSource("data/welcome.wav", 48000, 1, false);

  using Clock = std::chrono::steady_clock;
  auto next_deadline = Clock::now();
  while (running_flag.load(std::memory_order_relaxed)) {
    AudioFrame frame =
        AudioFrame::create(sample_rate, num_channels, samples_per_channel);
    wavSource.fillFrame(frame);
    try {
      source->captureFrame(frame);
    } catch (const std::exception &e) {
      std::cerr << "Error in captureFrame (noise): " << e.what() << std::endl;
      break;
    }

    // Pace the loop to roughly real-time
    next_deadline += std::chrono::milliseconds(frame_ms);
    std::this_thread::sleep_until(next_deadline);
  }

  try {
    source->clearQueue();
  } catch (...) {
    std::cout << "Error in clearQueue (noise)" << std::endl;
  }
}

// Fake video source: solid color cycling
void runFakeVideoCaptureLoop(const std::shared_ptr<VideoSource> &source,
                             std::atomic<bool> &running_flag) {
  auto frame = LKVideoFrame::create(1280, 720, VideoBufferType::BGRA);
  const double framerate = 1.0 / 30.0;

  while (running_flag.load(std::memory_order_relaxed)) {
    static auto start = std::chrono::high_resolution_clock::now();
    float t = std::chrono::duration<float>(
                  std::chrono::high_resolution_clock::now() - start)
                  .count();
    // Cycle every 4 seconds: 0=red, 1=green, 2=blue, 3=black
    int stage = static_cast<int>(t) % 4;

    std::array<uint8_t, 4> rgb{};
    switch (stage) {
    case 0: // red
      rgb = {255, 0, 0, 0};
      break;
    case 1: // green
      rgb = {0, 255, 0, 0};
      break;
    case 2: // blue
      rgb = {0, 0, 255, 0};
      break;
    case 3: // black
    default:
      rgb = {0, 0, 0, 0};
      break;
    }

    // ARGB
    uint8_t *data = frame.data();
    const size_t size = frame.dataSize();
    for (size_t i = 0; i < size; i += 4) {
      data[i + 0] = 255;    // A
      data[i + 1] = rgb[0]; // R
      data[i + 2] = rgb[1]; // G
      data[i + 3] = rgb[2]; // B
    }

    try {
      // If VideoSource is ARGB-capable, pass frame.
      // If it expects I420, pass i420 instead.
      source->captureFrame(frame, 0, VideoRotation::VIDEO_ROTATION_0);
    } catch (const std::exception &e) {
      std::cerr << "Error in captureFrame (fake video): " << e.what()
                << std::endl;
      break;
    }

    std::this_thread::sleep_for(std::chrono::duration<double>(framerate));
  }
}
