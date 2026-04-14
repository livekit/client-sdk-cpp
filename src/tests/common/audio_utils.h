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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <livekit/livekit.h>
#include <memory>
#include <thread>

namespace livekit::test {

// Default audio parameters for tests
constexpr int kDefaultAudioSampleRate = 48000;
constexpr int kDefaultAudioChannels = 1;
constexpr int kDefaultAudioFrameMs = 10;
constexpr int kDefaultSamplesPerChannel =
    kDefaultAudioSampleRate * kDefaultAudioFrameMs / 1000;

/// Create an AudioFrame with the given parameters for a 10ms duration.
/// @param sample_rate Sample rate in Hz.
/// @param num_channels Number of audio channels.
/// @return A new AudioFrame with the appropriate size.
inline AudioFrame create10msFrame(int sample_rate, int num_channels) {
  const int samples_per_channel = sample_rate / 100; // 10ms worth of samples
  return AudioFrame::create(sample_rate, num_channels, samples_per_channel);
}

/// Run an audio tone generation loop.
/// @param source The AudioSource to capture frames to.
/// @param running Atomic flag to control when to stop the loop.
/// @param base_freq_hz Base frequency in Hz for the tone.
/// @param siren_mode If true, modulates the frequency to create a siren effect.
/// @param sample_rate Audio sample rate (default: 48000).
/// @param num_channels Number of audio channels (default: 1).
/// @param frame_ms Duration of each frame in milliseconds (default: 10).
inline void runToneLoop(const std::shared_ptr<AudioSource> &source,
                        std::atomic<bool> &running, double base_freq_hz,
                        bool siren_mode,
                        int sample_rate = kDefaultAudioSampleRate,
                        int num_channels = kDefaultAudioChannels,
                        int frame_ms = kDefaultAudioFrameMs) {
  double phase = 0.0;
  constexpr double kTwoPi = 6.283185307179586;
  const int samples_per_channel = sample_rate * frame_ms / 1000;

  while (running.load(std::memory_order_relaxed)) {
    AudioFrame frame =
        AudioFrame::create(sample_rate, num_channels, samples_per_channel);
    auto &samples = frame.data();

    const double time_sec =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count()) /
        1000.0;
    const double freq =
        siren_mode ? (700.0 + 250.0 * std::sin(time_sec * 2.0)) : base_freq_hz;

    const double phase_inc = kTwoPi * freq / static_cast<double>(sample_rate);
    for (int i = 0; i < samples_per_channel; ++i) {
      samples[static_cast<std::size_t>(i)] =
          static_cast<std::int16_t>(std::sin(phase) * 12000.0);
      phase += phase_inc;
      if (phase > kTwoPi) {
        phase -= kTwoPi;
      }
    }

    try {
      source->captureFrame(frame);
    } catch (...) {
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(frame_ms));
  }
}

/// Fill an AudioFrame with a sine wave tone.
/// @param frame The AudioFrame to fill.
/// @param freq_hz Frequency of the tone in Hz.
/// @param sample_rate Sample rate in Hz.
/// @param phase Reference to phase accumulator (updated after call).
/// @param amplitude Amplitude of the tone (default: 12000).
inline void fillToneFrame(AudioFrame &frame, double freq_hz, int sample_rate,
                          double &phase, double amplitude = 12000.0) {
  constexpr double kTwoPi = 6.283185307179586;
  auto &samples = frame.data();
  const double phase_inc = kTwoPi * freq_hz / static_cast<double>(sample_rate);

  for (std::size_t i = 0; i < samples.size(); ++i) {
    samples[i] = static_cast<std::int16_t>(std::sin(phase) * amplitude);
    phase += phase_inc;
    if (phase > kTwoPi) {
      phase -= kTwoPi;
    }
  }
}

} // namespace livekit::test
