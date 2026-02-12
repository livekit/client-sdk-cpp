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

namespace livekit_bridge {
namespace test {

constexpr int kStressAudioSampleRate = 48000;
constexpr int kStressAudioChannels = 1;
constexpr int kStressFrameDurationMs = 10;
constexpr int kStressSamplesPerFrame =
    kStressAudioSampleRate * kStressFrameDurationMs / 1000;

static std::vector<std::int16_t> makeSineFrame(int samples, double freq,
                                                int &phase) {
  std::vector<std::int16_t> data(samples * kStressAudioChannels);
  const double amplitude = 16000.0;
  for (int i = 0; i < samples; ++i) {
    double t = static_cast<double>(phase++) / kStressAudioSampleRate;
    auto sample = static_cast<std::int16_t>(
        amplitude * std::sin(2.0 * M_PI * freq * t));
    for (int ch = 0; ch < kStressAudioChannels; ++ch) {
      data[i * kStressAudioChannels + ch] = sample;
    }
  }
  return data;
}

class BridgeAudioStressTest : public BridgeTestBase {};

// ---------------------------------------------------------------------------
// Sustained audio pushing: sends audio at real-time pace for the configured
// stress duration and tracks frames received, delivery rate, and errors.
// ---------------------------------------------------------------------------
TEST_F(BridgeAudioStressTest, SustainedAudioPush) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Sustained Audio Stress Test ===" << std::endl;
  std::cout << "Duration: " << config_.stress_duration_seconds << " seconds"
            << std::endl;

  LiveKitBridge caller;
  LiveKitBridge receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  auto audio_track = caller.createAudioTrack(
      "stress-mic", kStressAudioSampleRate, kStressAudioChannels,
      livekit::TrackSource::SOURCE_MICROPHONE);
  ASSERT_NE(audio_track, nullptr);

  const std::string caller_identity = "rpc-caller";

  std::atomic<int64_t> frames_sent{0};
  std::atomic<int64_t> frames_received{0};
  std::atomic<int64_t> push_failures{0};
  std::atomic<bool> running{true};

  receiver.setOnAudioFrameCallback(
      caller_identity, livekit::TrackSource::SOURCE_MICROPHONE,
      [&](const livekit::AudioFrame &) { frames_received++; });

  std::this_thread::sleep_for(3s);

  auto start_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::seconds(config_.stress_duration_seconds);

  std::thread sender([&]() {
    int phase = 0;
    auto next_frame_time = std::chrono::steady_clock::now();
    const auto frame_duration =
        std::chrono::milliseconds(kStressFrameDurationMs);

    while (running.load()) {
      std::this_thread::sleep_until(next_frame_time);
      next_frame_time += frame_duration;

      auto data = makeSineFrame(kStressSamplesPerFrame, 440.0, phase);
      bool ok = audio_track->pushFrame(data, kStressSamplesPerFrame);
      if (ok) {
        frames_sent++;
      } else {
        push_failures++;
      }
    }
  });

  std::thread progress([&]() {
    int64_t last_sent = 0;
    int64_t last_received = 0;
    while (running.load()) {
      std::this_thread::sleep_for(30s);
      if (!running.load())
        break;

      auto elapsed = std::chrono::steady_clock::now() - start_time;
      auto elapsed_s =
          std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
      int64_t cur_sent = frames_sent.load();
      int64_t cur_received = frames_received.load();
      double send_rate = (cur_sent - last_sent) / 30.0;
      double recv_rate = (cur_received - last_received) / 30.0;
      last_sent = cur_sent;
      last_received = cur_received;

      std::cout << "[" << elapsed_s << "s]"
                << " sent=" << cur_sent << " recv=" << cur_received
                << " failures=" << push_failures.load()
                << " send_rate=" << std::fixed << std::setprecision(1)
                << send_rate << "/s"
                << " recv_rate=" << recv_rate << "/s" << std::endl;
    }
  });

  while (std::chrono::steady_clock::now() - start_time < duration) {
    std::this_thread::sleep_for(1s);
  }

  running.store(false);
  sender.join();
  progress.join();

  std::this_thread::sleep_for(2s);

  std::cout << "\n========================================" << std::endl;
  std::cout << "  Sustained Audio Stress Results" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Duration:         " << config_.stress_duration_seconds << "s"
            << std::endl;
  std::cout << "Frames sent:      " << frames_sent.load() << std::endl;
  std::cout << "Frames received:  " << frames_received.load() << std::endl;
  std::cout << "Push failures:    " << push_failures.load() << std::endl;

  double delivery_rate =
      frames_sent.load() > 0
          ? (100.0 * frames_received.load() / frames_sent.load())
          : 0.0;
  std::cout << "Delivery rate:    " << std::fixed << std::setprecision(2)
            << delivery_rate << "%" << std::endl;
  std::cout << "========================================\n" << std::endl;

  EXPECT_GT(frames_sent.load(), 0) << "Should have sent frames";
  EXPECT_GT(frames_received.load(), 0) << "Should have received frames";
  EXPECT_EQ(push_failures.load(), 0) << "No push failures expected";
  EXPECT_GT(delivery_rate, 50.0) << "Delivery rate below 50%";

  receiver.clearOnAudioFrameCallback(
      caller_identity, livekit::TrackSource::SOURCE_MICROPHONE);
}

// ---------------------------------------------------------------------------
// Track release under active push: one thread pushes audio continuously
// while another releases the track after a delay. Verifies clean shutdown
// with no crashes or hangs.
// ---------------------------------------------------------------------------
TEST_F(BridgeAudioStressTest, ReleaseUnderActivePush) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Audio Release Under Active Push ===" << std::endl;

  const int iterations = config_.test_iterations;
  std::cout << "Iterations: " << iterations << std::endl;

  for (int iter = 0; iter < iterations; ++iter) {
    {
      LiveKitBridge bridge;
      livekit::RoomOptions options;
      options.auto_subscribe = true;

      bool connected =
          bridge.connect(config_.url, config_.caller_token, options);
      ASSERT_TRUE(connected) << "Iteration " << iter << ": connect failed";

      auto track = bridge.createAudioTrack(
          "release-stress-mic", kStressAudioSampleRate, kStressAudioChannels,
          livekit::TrackSource::SOURCE_MICROPHONE);

      std::atomic<bool> pushing{true};
      std::atomic<int> push_count{0};

      std::thread pusher([&]() {
        int phase = 0;
        while (pushing.load()) {
          auto data = makeSineFrame(kStressSamplesPerFrame, 440.0, phase);
          track->pushFrame(data, kStressSamplesPerFrame);
          push_count++;
          std::this_thread::sleep_for(
              std::chrono::milliseconds(kStressFrameDurationMs));
        }
      });

      std::this_thread::sleep_for(500ms);
      track->release();
      EXPECT_TRUE(track->isReleased());

      std::this_thread::sleep_for(200ms);
      pushing.store(false);
      pusher.join();

      std::cout << "  Iteration " << (iter + 1) << "/" << iterations
                << " OK (pushed " << push_count.load() << " frames)"
                << std::endl;
    } // bridge destroyed here

    std::this_thread::sleep_for(1s);
  }
}

// ---------------------------------------------------------------------------
// Rapid connect/disconnect with active audio callback. Verifies that the
// bridge's reader thread cleanup handles abrupt disconnection.
// ---------------------------------------------------------------------------
TEST_F(BridgeAudioStressTest, RapidConnectDisconnectWithCallback) {
  skipIfNotConfigured();

  const int cycles = config_.test_iterations;
  std::cout << "\n=== Bridge Rapid Connect/Disconnect With Audio Callback ==="
            << std::endl;
  std::cout << "Cycles: " << cycles << std::endl;

  const std::string caller_identity = "rpc-caller";

  for (int i = 0; i < cycles; ++i) {
    {
      LiveKitBridge caller;
      LiveKitBridge receiver;

      ASSERT_TRUE(connectPair(caller, receiver))
          << "Cycle " << i << ": connect failed";

      auto track = caller.createAudioTrack(
          "rapid-mic", kStressAudioSampleRate, kStressAudioChannels,
          livekit::TrackSource::SOURCE_MICROPHONE);

      std::atomic<int> rx_count{0};
      receiver.setOnAudioFrameCallback(
          caller_identity, livekit::TrackSource::SOURCE_MICROPHONE,
          [&](const livekit::AudioFrame &) { rx_count++; });

      int phase = 0;
      for (int f = 0; f < 50; ++f) {
        auto data = makeSineFrame(kStressSamplesPerFrame, 440.0, phase);
        track->pushFrame(data, kStressSamplesPerFrame);
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kStressFrameDurationMs));
      }

      // Clear callback to join reader thread while rx_count is still alive
      receiver.clearOnAudioFrameCallback(
          caller_identity, livekit::TrackSource::SOURCE_MICROPHONE);

      std::cout << "  Cycle " << (i + 1) << "/" << cycles
                << " OK (rx=" << rx_count.load() << ")" << std::endl;
    } // both bridges destroyed here

    std::this_thread::sleep_for(1s);
  }
}

} // namespace test
} // namespace livekit_bridge
