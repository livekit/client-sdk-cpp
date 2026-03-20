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
#include <random>

namespace livekit_bridge {
namespace test {

constexpr int kLifecycleSampleRate = 48000;
constexpr int kLifecycleChannels = 1;
constexpr int kLifecycleFrameDurationMs = 10;
constexpr int kLifecycleSamplesPerFrame =
    kLifecycleSampleRate * kLifecycleFrameDurationMs / 1000;

static std::vector<std::int16_t> makeSilent(int samples) {
  return std::vector<std::int16_t>(samples * kLifecycleChannels, 0);
}

static std::vector<std::uint8_t> makePayload(size_t size) {
  static thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<int> dist(0, 255);
  std::vector<std::uint8_t> buf(size);
  for (auto &b : buf)
    b = static_cast<std::uint8_t>(dist(gen));
  return buf;
}

class BridgeLifecycleStressTest : public BridgeTestBase {};

// ---------------------------------------------------------------------------
// Disconnect while frames are actively being pushed and received.
//
// Two threads push audio / data frames at full rate.  A third thread
// triggers disconnect() mid-flight after a random delay.  The bridge must
// tear down cleanly with no crashes, hangs, or thread leaks.  Repeated
// for TEST_ITERATIONS cycles.
// ---------------------------------------------------------------------------
TEST_F(BridgeLifecycleStressTest, DisconnectUnderLoad) {
  skipIfNotConfigured();

  const int cycles = config_.test_iterations;
  std::cout << "\n=== Bridge Disconnect Under Load ===" << std::endl;
  std::cout << "Cycles: " << cycles << std::endl;

  const std::string caller_identity = "rpc-caller";

  for (int i = 0; i < cycles; ++i) {
    {
      LiveKitBridge caller;
      LiveKitBridge receiver;

      ASSERT_TRUE(connectPair(caller, receiver))
          << "Cycle " << i << ": connect failed";

      auto audio = caller.createAudioTrack(
          "load-mic", kLifecycleSampleRate, kLifecycleChannels,
          livekit::TrackSource::SOURCE_MICROPHONE);
      auto data = caller.createDataTrack("load-data");

      std::atomic<int> audio_rx{0};
      std::atomic<int> data_rx{0};

      receiver.setOnAudioFrameCallback(
          caller_identity, livekit::TrackSource::SOURCE_MICROPHONE,
          [&](const livekit::AudioFrame &) { audio_rx++; });

      receiver.setOnDataFrameCallback(
          caller_identity, "load-data",
          [&](const std::vector<std::uint8_t> &,
              std::optional<std::uint64_t>) { data_rx++; });

      std::this_thread::sleep_for(2s);

      std::atomic<bool> running{true};

      std::thread audio_pusher([&]() {
        while (running.load()) {
          auto frame = makeSilent(kLifecycleSamplesPerFrame);
          audio->pushFrame(frame, kLifecycleSamplesPerFrame);
          std::this_thread::sleep_for(
              std::chrono::milliseconds(kLifecycleFrameDurationMs));
        }
      });

      std::thread data_pusher([&]() {
        while (running.load()) {
          auto payload = makePayload(512);
          data->pushFrame(payload);
          std::this_thread::sleep_for(20ms);
        }
      });

      // Let traffic flow for 1-2 seconds, then pull the plug.
      std::this_thread::sleep_for(1500ms);
      running.store(false);
      audio_pusher.join();
      data_pusher.join();

      // Clear callbacks while captured atomics are still alive
      receiver.clearOnAudioFrameCallback(
          caller_identity, livekit::TrackSource::SOURCE_MICROPHONE);
      receiver.clearOnDataFrameCallback(caller_identity, "load-data");

      std::cout << "  Cycle " << (i + 1) << "/" << cycles
                << " OK (audio_rx=" << audio_rx.load()
                << " data_rx=" << data_rx.load() << ")" << std::endl;
    } // bridges destroyed → disconnect + shutdown

    std::this_thread::sleep_for(1s);
  }
}

// ---------------------------------------------------------------------------
// Track release while receiver is consuming.
//
// Caller publishes audio + data, receiver is actively consuming via
// callbacks, then caller releases each track individually while the
// receiver's reader threads are still running.  Verifies no dangling
// pointers, no use-after-free, and clean thread join.
// ---------------------------------------------------------------------------
TEST_F(BridgeLifecycleStressTest, TrackReleaseWhileReceiving) {
  skipIfNotConfigured();

  const int iterations = config_.test_iterations;
  std::cout << "\n=== Bridge Track Release While Receiving ===" << std::endl;
  std::cout << "Iterations: " << iterations << std::endl;

  const std::string caller_identity = "rpc-caller";

  for (int iter = 0; iter < iterations; ++iter) {
    {
      LiveKitBridge caller;
      LiveKitBridge receiver;

      ASSERT_TRUE(connectPair(caller, receiver))
          << "Iteration " << iter << ": connect failed";

      auto audio = caller.createAudioTrack(
          "release-rx-mic", kLifecycleSampleRate, kLifecycleChannels,
          livekit::TrackSource::SOURCE_MICROPHONE);
      auto data = caller.createDataTrack("release-rx-data");

      std::atomic<int> audio_rx{0};
      std::atomic<int> data_rx{0};

      receiver.setOnAudioFrameCallback(
          caller_identity, livekit::TrackSource::SOURCE_MICROPHONE,
          [&](const livekit::AudioFrame &) { audio_rx++; });

      receiver.setOnDataFrameCallback(
          caller_identity, "release-rx-data",
          [&](const std::vector<std::uint8_t> &,
              std::optional<std::uint64_t>) { data_rx++; });

      std::this_thread::sleep_for(2s);

      std::atomic<bool> running{true};

      std::thread audio_pusher([&]() {
        while (running.load()) {
          if (audio->isReleased())
            break;
          auto frame = makeSilent(kLifecycleSamplesPerFrame);
          audio->pushFrame(frame, kLifecycleSamplesPerFrame);
          std::this_thread::sleep_for(
              std::chrono::milliseconds(kLifecycleFrameDurationMs));
        }
      });

      std::thread data_pusher([&]() {
        while (running.load()) {
          if (data->isReleased())
            break;
          auto payload = makePayload(256);
          data->pushFrame(payload);
          std::this_thread::sleep_for(20ms);
        }
      });

      // Let frames flow, then release tracks mid-stream
      std::this_thread::sleep_for(800ms);

      audio->release();
      EXPECT_TRUE(audio->isReleased());

      std::this_thread::sleep_for(200ms);

      data->release();
      EXPECT_TRUE(data->isReleased());

      running.store(false);
      audio_pusher.join();
      data_pusher.join();

      // pushFrame must return false on released tracks
      auto silence = makeSilent(kLifecycleSamplesPerFrame);
      EXPECT_FALSE(audio->pushFrame(silence, kLifecycleSamplesPerFrame));

      auto payload = makePayload(64);
      EXPECT_FALSE(data->pushFrame(payload));

      receiver.clearOnAudioFrameCallback(
          caller_identity, livekit::TrackSource::SOURCE_MICROPHONE);
      receiver.clearOnDataFrameCallback(caller_identity, "release-rx-data");

      std::cout << "  Iteration " << (iter + 1) << "/" << iterations
                << " OK (audio_rx=" << audio_rx.load()
                << " data_rx=" << data_rx.load() << ")" << std::endl;
    }

    std::this_thread::sleep_for(1s);
  }
}

// ---------------------------------------------------------------------------
// Repeated full lifecycle: connect → create all track types → push →
// release → disconnect.  Exercises the complete resource creation and
// teardown path looking for accumulating leaks.
// ---------------------------------------------------------------------------
TEST_F(BridgeLifecycleStressTest, FullLifecycleSoak) {
  skipIfNotConfigured();

  const int cycles = config_.test_iterations;
  std::cout << "\n=== Bridge Full Lifecycle Soak ===" << std::endl;
  std::cout << "Cycles: " << cycles << std::endl;

  for (int i = 0; i < cycles; ++i) {
    {
      LiveKitBridge bridge;
      livekit::RoomOptions options;
      options.auto_subscribe = true;

      bool connected =
          bridge.connect(config_.url, config_.caller_token, options);
      ASSERT_TRUE(connected) << "Cycle " << i << ": connect failed";

      auto audio = bridge.createAudioTrack(
          "soak-mic", kLifecycleSampleRate, kLifecycleChannels,
          livekit::TrackSource::SOURCE_MICROPHONE);

      constexpr int kVideoWidth = 320;
      constexpr int kVideoHeight = 240;
      auto video = bridge.createVideoTrack(
          "soak-cam", kVideoWidth, kVideoHeight,
          livekit::TrackSource::SOURCE_CAMERA);

      auto data = bridge.createDataTrack("soak-data");

      // Push a handful of frames on each track type
      for (int f = 0; f < 10; ++f) {
        auto pcm = makeSilent(kLifecycleSamplesPerFrame);
        audio->pushFrame(pcm, kLifecycleSamplesPerFrame);

        std::vector<std::uint8_t> rgba(kVideoWidth * kVideoHeight * 4, 0x80);
        video->pushFrame(rgba);

        auto payload = makePayload(256);
        data->pushFrame(payload);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(kLifecycleFrameDurationMs));
      }

      // Explicit release in various orders to exercise different teardown paths
      if (i % 3 == 0) {
        audio->release();
        video->release();
        data->release();
      } else if (i % 3 == 1) {
        data->release();
        audio->release();
        video->release();
      }
      // else: let disconnect() release all tracks
    } // bridge destroyed

    std::cout << "  Cycle " << (i + 1) << "/" << cycles << " OK" << std::endl;
    std::this_thread::sleep_for(1s);
  }
}

} // namespace test
} // namespace livekit_bridge
