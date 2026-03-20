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

constexpr int kCbSampleRate = 48000;
constexpr int kCbChannels = 1;
constexpr int kCbFrameDurationMs = 10;
constexpr int kCbSamplesPerFrame =
    kCbSampleRate * kCbFrameDurationMs / 1000;

static std::vector<std::int16_t> cbSilentFrame() {
  return std::vector<std::int16_t>(kCbSamplesPerFrame * kCbChannels, 0);
}

static std::vector<std::uint8_t> cbPayload(size_t size) {
  static thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<int> dist(0, 255);
  std::vector<std::uint8_t> buf(size);
  for (auto &b : buf)
    b = static_cast<std::uint8_t>(dist(gen));
  return buf;
}

class BridgeCallbackStressTest : public BridgeTestBase {};

// ---------------------------------------------------------------------------
// Audio callback churn.
//
// Caller pushes audio at real-time pace.  A separate thread rapidly
// registers / clears the receiver's audio callback.  Each clear() must
// join the reader thread (which closes the AudioStream), and the
// subsequent register must start a new reader.  This hammers the
// extract-thread-outside-lock pattern in clearOnAudioFrameCallback().
// ---------------------------------------------------------------------------
TEST_F(BridgeCallbackStressTest, AudioCallbackChurn) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Audio Callback Churn ===" << std::endl;

  LiveKitBridge caller;
  LiveKitBridge receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  auto audio = caller.createAudioTrack(
      "churn-mic", kCbSampleRate, kCbChannels,
      livekit::TrackSource::SOURCE_MICROPHONE);
  ASSERT_NE(audio, nullptr);

  const std::string caller_identity = "rpc-caller";

  std::atomic<bool> running{true};
  std::atomic<int64_t> total_rx{0};
  std::atomic<int> churn_cycles{0};

  std::thread pusher([&]() {
    auto next = std::chrono::steady_clock::now();
    while (running.load()) {
      std::this_thread::sleep_until(next);
      next += std::chrono::milliseconds(kCbFrameDurationMs);
      auto frame = cbSilentFrame();
      audio->pushFrame(frame, kCbSamplesPerFrame);
    }
  });

  std::thread churner([&]() {
    while (running.load()) {
      receiver.setOnAudioFrameCallback(
          caller_identity, livekit::TrackSource::SOURCE_MICROPHONE,
          [&](const livekit::AudioFrame &) { total_rx++; });

      std::this_thread::sleep_for(300ms);

      receiver.clearOnAudioFrameCallback(
          caller_identity, livekit::TrackSource::SOURCE_MICROPHONE);

      std::this_thread::sleep_for(100ms);
      churn_cycles++;
    }
  });

  const int duration_s = std::min(config_.stress_duration_seconds, 30);
  std::this_thread::sleep_for(std::chrono::seconds(duration_s));

  running.store(false);
  pusher.join();
  churner.join();

  std::cout << "Churn cycles: " << churn_cycles.load() << std::endl;
  std::cout << "Audio frames received: " << total_rx.load() << std::endl;

  EXPECT_GT(churn_cycles.load(), 0);
}

// ---------------------------------------------------------------------------
// Mixed audio + data callback churn.
//
// Caller publishes both an audio and data track.  Two independent churn
// threads each toggle their respective callback.  A third thread pushes
// frames on both tracks.  This exercises the bridge's two independent
// callback maps and reader sets under concurrent mutation.
// ---------------------------------------------------------------------------
TEST_F(BridgeCallbackStressTest, MixedCallbackChurn) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Mixed Callback Churn ===" << std::endl;

  LiveKitBridge caller;
  LiveKitBridge receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  auto audio = caller.createAudioTrack(
      "mixed-mic", kCbSampleRate, kCbChannels,
      livekit::TrackSource::SOURCE_MICROPHONE);
  auto data = caller.createDataTrack("mixed-data");
  ASSERT_NE(audio, nullptr);
  ASSERT_NE(data, nullptr);

  const std::string caller_identity = "rpc-caller";

  std::atomic<bool> running{true};
  std::atomic<int64_t> audio_rx{0};
  std::atomic<int64_t> data_rx{0};
  std::atomic<int> audio_churns{0};
  std::atomic<int> data_churns{0};

  std::thread audio_pusher([&]() {
    auto next = std::chrono::steady_clock::now();
    while (running.load()) {
      std::this_thread::sleep_until(next);
      next += std::chrono::milliseconds(kCbFrameDurationMs);
      auto frame = cbSilentFrame();
      audio->pushFrame(frame, kCbSamplesPerFrame);
    }
  });

  std::thread data_pusher([&]() {
    while (running.load()) {
      auto payload = cbPayload(256);
      data->pushFrame(payload);
      std::this_thread::sleep_for(20ms);
    }
  });

  std::thread audio_churner([&]() {
    while (running.load()) {
      receiver.setOnAudioFrameCallback(
          caller_identity, livekit::TrackSource::SOURCE_MICROPHONE,
          [&](const livekit::AudioFrame &) { audio_rx++; });
      std::this_thread::sleep_for(250ms);
      receiver.clearOnAudioFrameCallback(
          caller_identity, livekit::TrackSource::SOURCE_MICROPHONE);
      std::this_thread::sleep_for(100ms);
      audio_churns++;
    }
  });

  std::thread data_churner([&]() {
    while (running.load()) {
      receiver.setOnDataFrameCallback(
          caller_identity, "mixed-data",
          [&](const std::vector<std::uint8_t> &,
              std::optional<std::uint64_t>) { data_rx++; });
      std::this_thread::sleep_for(350ms);
      receiver.clearOnDataFrameCallback(caller_identity, "mixed-data");
      std::this_thread::sleep_for(150ms);
      data_churns++;
    }
  });

  const int duration_s = std::min(config_.stress_duration_seconds, 30);
  std::this_thread::sleep_for(std::chrono::seconds(duration_s));

  running.store(false);
  audio_pusher.join();
  data_pusher.join();
  audio_churner.join();
  data_churner.join();

  std::cout << "Audio churn cycles:  " << audio_churns.load() << std::endl;
  std::cout << "Data churn cycles:   " << data_churns.load() << std::endl;
  std::cout << "Audio frames rx:     " << audio_rx.load() << std::endl;
  std::cout << "Data frames rx:      " << data_rx.load() << std::endl;

  EXPECT_GT(audio_churns.load(), 0);
  EXPECT_GT(data_churns.load(), 0);
}

// ---------------------------------------------------------------------------
// Callback replacement storm.
//
// Instead of clear + set, rapidly replace the callback with a new lambda
// (calling setOnAudioFrameCallback twice without a clear in between).
// The bridge should silently overwrite the old callback and the new one
// should start receiving.  The old reader thread should eventually be
// replaced the next time onTrackSubscribed fires.
// ---------------------------------------------------------------------------
TEST_F(BridgeCallbackStressTest, CallbackReplacement) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Callback Replacement Storm ===" << std::endl;

  LiveKitBridge caller;
  LiveKitBridge receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  auto audio = caller.createAudioTrack(
      "replace-mic", kCbSampleRate, kCbChannels,
      livekit::TrackSource::SOURCE_MICROPHONE);
  ASSERT_NE(audio, nullptr);

  const std::string caller_identity = "rpc-caller";

  std::atomic<bool> running{true};
  std::atomic<int64_t> total_rx{0};
  std::atomic<int> replacements{0};

  std::thread pusher([&]() {
    auto next = std::chrono::steady_clock::now();
    while (running.load()) {
      std::this_thread::sleep_until(next);
      next += std::chrono::milliseconds(kCbFrameDurationMs);
      auto frame = cbSilentFrame();
      audio->pushFrame(frame, kCbSamplesPerFrame);
    }
  });

  std::thread replacer([&]() {
    while (running.load()) {
      receiver.setOnAudioFrameCallback(
          caller_identity, livekit::TrackSource::SOURCE_MICROPHONE,
          [&](const livekit::AudioFrame &) { total_rx++; });
      replacements++;
      std::this_thread::sleep_for(100ms);
    }
  });

  const int duration_s = std::min(config_.stress_duration_seconds, 20);
  std::this_thread::sleep_for(std::chrono::seconds(duration_s));

  running.store(false);
  pusher.join();
  replacer.join();

  // Final clear to join any lingering reader
  receiver.clearOnAudioFrameCallback(
      caller_identity, livekit::TrackSource::SOURCE_MICROPHONE);

  std::cout << "Replacements:   " << replacements.load() << std::endl;
  std::cout << "Total frames rx: " << total_rx.load() << std::endl;

  EXPECT_GT(replacements.load(), 0);
}

} // namespace test
} // namespace livekit_bridge
