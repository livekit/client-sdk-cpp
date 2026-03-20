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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../common/bridge_test_common.h"
#include <cmath>
#include <condition_variable>

namespace livekit_bridge {
namespace test {

constexpr int kAudioSampleRate = 48000;
constexpr int kAudioChannels = 1;
constexpr int kAudioFrameDurationMs = 10;
constexpr int kSamplesPerFrame =
    kAudioSampleRate * kAudioFrameDurationMs / 1000;
constexpr double kHighEnergyThreshold = 0.3;
constexpr int kHighEnergyFramesPerPulse = 5;

static std::vector<std::int16_t> generateHighEnergyFrame(int samples) {
  std::vector<std::int16_t> data(samples * kAudioChannels);
  const double frequency = 1000.0;
  const double amplitude = 30000.0;
  for (int i = 0; i < samples; ++i) {
    double t = static_cast<double>(i) / kAudioSampleRate;
    auto sample = static_cast<std::int16_t>(
        amplitude * std::sin(2.0 * M_PI * frequency * t));
    for (int ch = 0; ch < kAudioChannels; ++ch) {
      data[i * kAudioChannels + ch] = sample;
    }
  }
  return data;
}

static std::vector<std::int16_t> generateSilentFrame(int samples) {
  return std::vector<std::int16_t>(samples * kAudioChannels, 0);
}

static double calculateEnergy(const std::vector<std::int16_t> &samples) {
  if (samples.empty())
    return 0.0;
  double sum_squared = 0.0;
  for (auto s : samples) {
    double normalized = static_cast<double>(s) / 32768.0;
    sum_squared += normalized * normalized;
  }
  return std::sqrt(sum_squared / samples.size());
}

class BridgeAudioRoundtripTest : public BridgeTestBase {};

// ---------------------------------------------------------------------------
// Test 1: Basic audio frame round-trip through the bridge API.
//
// Caller bridge publishes an audio track, receiver bridge receives frames via
// setOnAudioFrameCallback.  We send high-energy pulses interleaved with
// silence and verify that the receiver detects them.
// ---------------------------------------------------------------------------
TEST_F(BridgeAudioRoundtripTest, AudioFrameRoundTrip) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Audio Frame Round-Trip Test ===" << std::endl;

  LiveKitBridge caller;
  LiveKitBridge receiver;

  ASSERT_TRUE(connectPair(caller, receiver))
      << "Failed to connect caller/receiver pair";

  std::cout << "Both bridges connected." << std::endl;

  auto audio_track = caller.createAudioTrack(
      "roundtrip-mic", kAudioSampleRate, kAudioChannels,
      livekit::TrackSource::SOURCE_MICROPHONE);
  ASSERT_NE(audio_track, nullptr);

  std::cout << "Audio track published." << std::endl;

  std::atomic<int> frames_received{0};
  std::atomic<int> high_energy_frames{0};

  const std::string caller_identity = "rpc-caller";

  receiver.setOnAudioFrameCallback(
      caller_identity, livekit::TrackSource::SOURCE_MICROPHONE,
      [&](const livekit::AudioFrame &frame) {
        frames_received++;
        double energy = calculateEnergy(frame.data());
        if (energy > kHighEnergyThreshold) {
          high_energy_frames++;
        }
      });

  std::cout << "Callback registered, sending audio..." << std::endl;

  const int total_frames = 500;
  const int frames_between_pulses = 100;
  auto next_frame_time = std::chrono::steady_clock::now();
  const auto frame_duration = std::chrono::milliseconds(kAudioFrameDurationMs);
  int pulses_sent = 0;
  int high_energy_remaining = 0;

  for (int i = 0; i < total_frames; ++i) {
    std::this_thread::sleep_until(next_frame_time);
    next_frame_time += frame_duration;

    std::vector<std::int16_t> frame_data;

    if (high_energy_remaining > 0) {
      frame_data = generateHighEnergyFrame(kSamplesPerFrame);
      high_energy_remaining--;
    } else if (i > 0 && i % frames_between_pulses == 0) {
      frame_data = generateHighEnergyFrame(kSamplesPerFrame);
      high_energy_remaining = kHighEnergyFramesPerPulse - 1;
      pulses_sent++;
      std::cout << "  Sent pulse " << pulses_sent << std::endl;
    } else {
      frame_data = generateSilentFrame(kSamplesPerFrame);
    }

    audio_track->pushFrame(frame_data, kSamplesPerFrame);
  }

  std::this_thread::sleep_for(2s);

  std::cout << "\nResults:" << std::endl;
  std::cout << "  Pulses sent:            " << pulses_sent << std::endl;
  std::cout << "  Frames received:        " << frames_received.load()
            << std::endl;
  std::cout << "  High-energy frames rx:  " << high_energy_frames.load()
            << std::endl;

  EXPECT_GT(frames_received.load(), 0)
      << "Receiver should have received at least one audio frame";
  EXPECT_GT(high_energy_frames.load(), 0)
      << "Receiver should have detected at least one high-energy pulse";

  // Clear callback before bridges go out of scope so the reader thread
  // is joined while the atomic counters above are still alive.
  receiver.clearOnAudioFrameCallback(
      caller_identity, livekit::TrackSource::SOURCE_MICROPHONE);
}

// ---------------------------------------------------------------------------
// Test 2: Audio latency measurement through the bridge.
//
// Same energy-detection approach as the base SDK's AudioLatency test but
// exercises the full bridge pipeline: pushFrame() → SFU → reader thread →
// AudioFrameCallback.
// ---------------------------------------------------------------------------
TEST_F(BridgeAudioRoundtripTest, AudioLatencyMeasurement) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Audio Latency Measurement ===" << std::endl;

  LiveKitBridge caller;
  LiveKitBridge receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  auto audio_track = caller.createAudioTrack(
      "latency-mic", kAudioSampleRate, kAudioChannels,
      livekit::TrackSource::SOURCE_MICROPHONE);
  ASSERT_NE(audio_track, nullptr);

  LatencyStats stats;
  std::atomic<bool> running{true};
  std::atomic<uint64_t> last_send_time_us{0};
  std::atomic<bool> waiting_for_echo{false};
  std::atomic<int> missed_pulses{0};
  constexpr uint64_t kEchoTimeoutUs = 2'000'000;

  const std::string caller_identity = "rpc-caller";

  receiver.setOnAudioFrameCallback(
      caller_identity, livekit::TrackSource::SOURCE_MICROPHONE,
      [&](const livekit::AudioFrame &frame) {
        double energy = calculateEnergy(frame.data());
        if (waiting_for_echo.load() && energy > kHighEnergyThreshold) {
          uint64_t rx_us =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now().time_since_epoch())
                  .count();
          uint64_t tx_us = last_send_time_us.load();
          if (tx_us > 0) {
            double latency_ms = (rx_us - tx_us) / 1000.0;
            if (latency_ms > 0 && latency_ms < 5000) {
              stats.addMeasurement(latency_ms);
              std::cout << "  Latency: " << std::fixed << std::setprecision(2)
                        << latency_ms << " ms" << std::endl;
            }
            waiting_for_echo.store(false);
          }
        }
      });

  const int total_pulses = 10;
  const int frames_between_pulses = 100;
  int pulses_sent = 0;
  int high_energy_remaining = 0;
  uint64_t pulse_send_time = 0;

  auto next_frame_time = std::chrono::steady_clock::now();
  const auto frame_duration = std::chrono::milliseconds(kAudioFrameDurationMs);
  int frame_count = 0;

  while (running.load() && pulses_sent < total_pulses) {
    std::this_thread::sleep_until(next_frame_time);
    next_frame_time += frame_duration;

    if (waiting_for_echo.load() && pulse_send_time > 0) {
      uint64_t now_us =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count();
      if (now_us - pulse_send_time > kEchoTimeoutUs) {
        std::cout << "  Echo timeout for pulse " << pulses_sent << std::endl;
        waiting_for_echo.store(false);
        missed_pulses++;
        pulse_send_time = 0;
        high_energy_remaining = 0;
      }
    }

    std::vector<std::int16_t> frame_data;

    if (high_energy_remaining > 0) {
      frame_data = generateHighEnergyFrame(kSamplesPerFrame);
      high_energy_remaining--;
    } else if (frame_count > 0 &&
               frame_count % frames_between_pulses == 0 &&
               !waiting_for_echo.load()) {
      frame_data = generateHighEnergyFrame(kSamplesPerFrame);
      high_energy_remaining = kHighEnergyFramesPerPulse - 1;

      pulse_send_time =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count();
      last_send_time_us.store(pulse_send_time);
      waiting_for_echo.store(true);
      pulses_sent++;
      std::cout << "Sent pulse " << pulses_sent << "/" << total_pulses
                << std::endl;
    } else {
      frame_data = generateSilentFrame(kSamplesPerFrame);
    }

    audio_track->pushFrame(frame_data, kSamplesPerFrame);
    frame_count++;
  }

  std::this_thread::sleep_for(2s);

  stats.printStats("Bridge Audio Latency Statistics");

  if (missed_pulses > 0) {
    std::cout << "Missed pulses (timeout): " << missed_pulses << std::endl;
  }

  EXPECT_GT(stats.count(), 0u)
      << "At least one audio latency measurement should be recorded";

  receiver.clearOnAudioFrameCallback(
      caller_identity, livekit::TrackSource::SOURCE_MICROPHONE);
}

// ---------------------------------------------------------------------------
// Test 3: Connect → publish → disconnect cycle for audio tracks.
//
// Each cycle creates a bridge in a scoped block so the destructor runs
// before the next cycle begins.  A sleep between cycles gives the Rust
// runtime time to fully tear down.
// ---------------------------------------------------------------------------
TEST_F(BridgeAudioRoundtripTest, ConnectPublishDisconnectCycle) {
  skipIfNotConfigured();

  const int cycles = config_.test_iterations;
  std::cout << "\n=== Bridge Audio Connect/Disconnect Cycles ===" << std::endl;
  std::cout << "Cycles: " << cycles << std::endl;

  for (int i = 0; i < cycles; ++i) {
    {
      LiveKitBridge bridge;
      livekit::RoomOptions options;
      options.auto_subscribe = true;

      bool connected =
          bridge.connect(config_.url, config_.caller_token, options);
      ASSERT_TRUE(connected) << "Cycle " << i << ": connect failed";

      auto track = bridge.createAudioTrack(
          "cycle-mic", kAudioSampleRate, kAudioChannels,
          livekit::TrackSource::SOURCE_MICROPHONE);
      ASSERT_NE(track, nullptr);

      for (int f = 0; f < 10; ++f) {
        auto data = generateSilentFrame(kSamplesPerFrame);
        track->pushFrame(data, kSamplesPerFrame);
      }
    } // bridge destroyed here → disconnect + shutdown

    std::cout << "  Cycle " << (i + 1) << "/" << cycles << " OK" << std::endl;
    std::this_thread::sleep_for(1s);
  }
}

} // namespace test
} // namespace livekit_bridge
