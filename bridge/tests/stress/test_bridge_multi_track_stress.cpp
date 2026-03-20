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

constexpr int kMtSampleRate = 48000;
constexpr int kMtChannels = 1;
constexpr int kMtFrameDurationMs = 10;
constexpr int kMtSamplesPerFrame = kMtSampleRate * kMtFrameDurationMs / 1000;
constexpr int kMtVideoWidth = 160;
constexpr int kMtVideoHeight = 120;
constexpr size_t kMtVideoFrameBytes = kMtVideoWidth * kMtVideoHeight * 4;

static std::vector<std::int16_t> mtSilentFrame() {
  return std::vector<std::int16_t>(kMtSamplesPerFrame * kMtChannels, 0);
}

static std::vector<std::uint8_t> mtVideoFrame() {
  return std::vector<std::uint8_t>(kMtVideoFrameBytes, 0x42);
}

static std::vector<std::uint8_t> mtPayload(size_t size) {
  static thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<int> dist(0, 255);
  std::vector<std::uint8_t> buf(size);
  for (auto &b : buf)
    b = static_cast<std::uint8_t>(dist(gen));
  return buf;
}

class BridgeMultiTrackStressTest : public BridgeTestBase {};

// ---------------------------------------------------------------------------
// Concurrent pushes on all track types.
//
// Publishes an audio track, a video track, and two data tracks.  A
// separate thread pushes frames on each track simultaneously for the
// configured stress duration.  All four threads contend on the bridge's
// internal mutex and on the underlying FFI.  Reports per-track push
// success rates.
// ---------------------------------------------------------------------------
TEST_F(BridgeMultiTrackStressTest, ConcurrentMultiTrackPush) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Concurrent Multi-Track Push ===" << std::endl;
  std::cout << "Duration: " << config_.stress_duration_seconds << "s"
            << std::endl;

  LiveKitBridge bridge;
  livekit::RoomOptions options;
  options.auto_subscribe = true;

  bool connected = bridge.connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(connected);

  auto audio = bridge.createAudioTrack(
      "mt-mic", kMtSampleRate, kMtChannels,
      livekit::TrackSource::SOURCE_MICROPHONE);

  auto video = bridge.createVideoTrack(
      "mt-cam", kMtVideoWidth, kMtVideoHeight,
      livekit::TrackSource::SOURCE_CAMERA);

  auto data1 = bridge.createDataTrack("mt-data-1");
  auto data2 = bridge.createDataTrack("mt-data-2");

  ASSERT_NE(audio, nullptr);
  ASSERT_NE(video, nullptr);
  ASSERT_NE(data1, nullptr);
  ASSERT_NE(data2, nullptr);

  struct TrackStats {
    std::atomic<int64_t> pushes{0};
    std::atomic<int64_t> successes{0};
    std::atomic<int64_t> failures{0};
  };

  TrackStats audio_stats, video_stats, data1_stats, data2_stats;
  std::atomic<bool> running{true};

  auto start_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::seconds(config_.stress_duration_seconds);

  std::thread audio_thread([&]() {
    auto next = std::chrono::steady_clock::now();
    while (running.load()) {
      std::this_thread::sleep_until(next);
      next += std::chrono::milliseconds(kMtFrameDurationMs);
      auto frame = mtSilentFrame();
      bool ok = audio->pushFrame(frame, kMtSamplesPerFrame);
      audio_stats.pushes++;
      if (ok)
        audio_stats.successes++;
      else
        audio_stats.failures++;
    }
  });

  std::thread video_thread([&]() {
    while (running.load()) {
      auto frame = mtVideoFrame();
      bool ok = video->pushFrame(frame);
      video_stats.pushes++;
      if (ok)
        video_stats.successes++;
      else
        video_stats.failures++;
      // ~30 fps
      std::this_thread::sleep_for(33ms);
    }
  });

  std::thread data1_thread([&]() {
    while (running.load()) {
      auto payload = mtPayload(512);
      bool ok = data1->pushFrame(payload);
      data1_stats.pushes++;
      if (ok)
        data1_stats.successes++;
      else
        data1_stats.failures++;
      std::this_thread::sleep_for(10ms);
    }
  });

  std::thread data2_thread([&]() {
    while (running.load()) {
      auto payload = mtPayload(2048);
      bool ok = data2->pushFrame(payload);
      data2_stats.pushes++;
      if (ok)
        data2_stats.successes++;
      else
        data2_stats.failures++;
      std::this_thread::sleep_for(15ms);
    }
  });

  // Progress reporting
  std::thread progress([&]() {
    while (running.load()) {
      std::this_thread::sleep_for(30s);
      if (!running.load())
        break;
      auto elapsed = std::chrono::steady_clock::now() - start_time;
      auto elapsed_s =
          std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
      std::cout << "[" << elapsed_s << "s]"
                << " audio=" << audio_stats.pushes.load()
                << " video=" << video_stats.pushes.load()
                << " data1=" << data1_stats.pushes.load()
                << " data2=" << data2_stats.pushes.load() << std::endl;
    }
  });

  while (std::chrono::steady_clock::now() - start_time < duration) {
    std::this_thread::sleep_for(1s);
  }

  running.store(false);
  audio_thread.join();
  video_thread.join();
  data1_thread.join();
  data2_thread.join();
  progress.join();

  auto printTrack = [](const char *name, const TrackStats &s) {
    double rate = s.pushes.load() > 0
                      ? (100.0 * s.successes.load() / s.pushes.load())
                      : 0.0;
    std::cout << "  " << name << ": pushes=" << s.pushes.load()
              << " ok=" << s.successes.load()
              << " fail=" << s.failures.load() << " (" << std::fixed
              << std::setprecision(1) << rate << "%)" << std::endl;
  };

  std::cout << "\n========================================" << std::endl;
  std::cout << "  Multi-Track Push Results" << std::endl;
  std::cout << "========================================" << std::endl;
  printTrack("audio ", audio_stats);
  printTrack("video ", video_stats);
  printTrack("data-1", data1_stats);
  printTrack("data-2", data2_stats);
  std::cout << "========================================\n" << std::endl;

  EXPECT_GT(audio_stats.successes.load(), 0);
  EXPECT_GT(video_stats.successes.load(), 0);
  EXPECT_GT(data1_stats.successes.load(), 0);
  EXPECT_GT(data2_stats.successes.load(), 0);
}

// ---------------------------------------------------------------------------
// Concurrent track creation and release.
//
// Multiple threads simultaneously create tracks, push a short burst,
// then release them.  Exercises the bridge's published_*_tracks_ vectors
// and mutex under heavy concurrent modification.
// ---------------------------------------------------------------------------
TEST_F(BridgeMultiTrackStressTest, ConcurrentCreateRelease) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Concurrent Track Create/Release ===" << std::endl;

  LiveKitBridge bridge;
  livekit::RoomOptions options;
  options.auto_subscribe = true;

  bool connected = bridge.connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(connected);

  std::atomic<bool> running{true};
  std::atomic<int> audio_cycles{0};
  std::atomic<int> data_cycles{0};
  std::atomic<int> errors{0};

  // Each source can only have one active track, so we serialize by source
  // but run the two sources concurrently.

  std::thread audio_thread([&]() {
    while (running.load()) {
      try {
        auto track = bridge.createAudioTrack(
            "create-release-mic", kMtSampleRate, kMtChannels,
            livekit::TrackSource::SOURCE_MICROPHONE);

        for (int i = 0; i < 5; ++i) {
          auto frame = mtSilentFrame();
          track->pushFrame(frame, kMtSamplesPerFrame);
          std::this_thread::sleep_for(
              std::chrono::milliseconds(kMtFrameDurationMs));
        }

        track->release();
        audio_cycles++;
      } catch (const std::exception &e) {
        errors++;
        std::cerr << "Audio create/release error: " << e.what() << std::endl;
      }
      std::this_thread::sleep_for(200ms);
    }
  });

  std::thread data_thread([&]() {
    int track_counter = 0;
    while (running.load()) {
      try {
        auto track = bridge.createDataTrack(
            "create-release-data-" + std::to_string(track_counter++));

        for (int i = 0; i < 5; ++i) {
          auto payload = mtPayload(128);
          track->pushFrame(payload);
          std::this_thread::sleep_for(20ms);
        }

        track->release();
        data_cycles++;
      } catch (const std::exception &e) {
        errors++;
        std::cerr << "Data create/release error: " << e.what() << std::endl;
      }
      std::this_thread::sleep_for(200ms);
    }
  });

  const int duration_s = std::min(config_.stress_duration_seconds, 30);
  std::this_thread::sleep_for(std::chrono::seconds(duration_s));

  running.store(false);
  audio_thread.join();
  data_thread.join();

  std::cout << "Audio create/release cycles: " << audio_cycles.load()
            << std::endl;
  std::cout << "Data create/release cycles:  " << data_cycles.load()
            << std::endl;
  std::cout << "Errors: " << errors.load() << std::endl;

  EXPECT_GT(audio_cycles.load(), 0);
  EXPECT_GT(data_cycles.load(), 0);
  EXPECT_EQ(errors.load(), 0);
}

// ---------------------------------------------------------------------------
// Full-duplex multi-track.
//
// Both caller and receiver publish audio + data tracks.  Both register
// callbacks for the other's tracks.  All four push-threads and all four
// reader threads run simultaneously, exercising the bridge's internal
// maps from both the publish side and the subscribe side.
// ---------------------------------------------------------------------------
TEST_F(BridgeMultiTrackStressTest, FullDuplexMultiTrack) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Full-Duplex Multi-Track ===" << std::endl;
  std::cout << "Duration: " << config_.stress_duration_seconds << "s"
            << std::endl;

  LiveKitBridge caller;
  LiveKitBridge receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  const std::string caller_identity = "rpc-caller";
  const std::string receiver_identity = "rpc-receiver";

  // Caller publishes
  auto caller_audio = caller.createAudioTrack(
      "duplex-mic", kMtSampleRate, kMtChannels,
      livekit::TrackSource::SOURCE_MICROPHONE);
  auto caller_data = caller.createDataTrack("duplex-data-caller");

  // Receiver publishes
  auto receiver_audio = receiver.createAudioTrack(
      "duplex-mic", kMtSampleRate, kMtChannels,
      livekit::TrackSource::SOURCE_MICROPHONE);
  auto receiver_data = receiver.createDataTrack("duplex-data-receiver");

  // Cross-register callbacks
  std::atomic<int64_t> caller_audio_rx{0};
  std::atomic<int64_t> caller_data_rx{0};
  std::atomic<int64_t> receiver_audio_rx{0};
  std::atomic<int64_t> receiver_data_rx{0};

  caller.setOnAudioFrameCallback(
      receiver_identity, livekit::TrackSource::SOURCE_MICROPHONE,
      [&](const livekit::AudioFrame &) { caller_audio_rx++; });
  caller.setOnDataFrameCallback(
      receiver_identity, "duplex-data-receiver",
      [&](const std::vector<std::uint8_t> &,
          std::optional<std::uint64_t>) { caller_data_rx++; });

  receiver.setOnAudioFrameCallback(
      caller_identity, livekit::TrackSource::SOURCE_MICROPHONE,
      [&](const livekit::AudioFrame &) { receiver_audio_rx++; });
  receiver.setOnDataFrameCallback(
      caller_identity, "duplex-data-caller",
      [&](const std::vector<std::uint8_t> &,
          std::optional<std::uint64_t>) { receiver_data_rx++; });

  std::this_thread::sleep_for(3s);

  std::atomic<bool> running{true};
  auto start_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::seconds(config_.stress_duration_seconds);

  auto audio_push_fn =
      [&](std::shared_ptr<BridgeAudioTrack> track) {
        auto next = std::chrono::steady_clock::now();
        while (running.load()) {
          std::this_thread::sleep_until(next);
          next += std::chrono::milliseconds(kMtFrameDurationMs);
          auto frame = mtSilentFrame();
          track->pushFrame(frame, kMtSamplesPerFrame);
        }
      };

  auto data_push_fn =
      [&](std::shared_ptr<BridgeDataTrack> track) {
        while (running.load()) {
          auto payload = mtPayload(256);
          track->pushFrame(payload);
          std::this_thread::sleep_for(20ms);
        }
      };

  std::thread t1(audio_push_fn, caller_audio);
  std::thread t2(data_push_fn, caller_data);
  std::thread t3(audio_push_fn, receiver_audio);
  std::thread t4(data_push_fn, receiver_data);

  std::thread progress([&]() {
    while (running.load()) {
      std::this_thread::sleep_for(30s);
      if (!running.load())
        break;
      auto elapsed = std::chrono::steady_clock::now() - start_time;
      auto elapsed_s =
          std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
      std::cout << "[" << elapsed_s << "s]"
                << " caller_audio_rx=" << caller_audio_rx.load()
                << " caller_data_rx=" << caller_data_rx.load()
                << " receiver_audio_rx=" << receiver_audio_rx.load()
                << " receiver_data_rx=" << receiver_data_rx.load()
                << std::endl;
    }
  });

  while (std::chrono::steady_clock::now() - start_time < duration) {
    std::this_thread::sleep_for(1s);
  }

  running.store(false);
  t1.join();
  t2.join();
  t3.join();
  t4.join();
  progress.join();

  std::cout << "\n========================================" << std::endl;
  std::cout << "  Full-Duplex Multi-Track Results" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Caller   audio rx: " << caller_audio_rx.load() << std::endl;
  std::cout << "Caller   data  rx: " << caller_data_rx.load() << std::endl;
  std::cout << "Receiver audio rx: " << receiver_audio_rx.load() << std::endl;
  std::cout << "Receiver data  rx: " << receiver_data_rx.load() << std::endl;
  std::cout << "========================================\n" << std::endl;

  EXPECT_GT(receiver_audio_rx.load(), 0);
  EXPECT_GT(receiver_data_rx.load(), 0);

  // Clear callbacks while atomics are alive
  caller.clearOnAudioFrameCallback(
      receiver_identity, livekit::TrackSource::SOURCE_MICROPHONE);
  caller.clearOnDataFrameCallback(receiver_identity, "duplex-data-receiver");
  receiver.clearOnAudioFrameCallback(
      caller_identity, livekit::TrackSource::SOURCE_MICROPHONE);
  receiver.clearOnDataFrameCallback(caller_identity, "duplex-data-caller");
}

} // namespace test
} // namespace livekit_bridge
