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
#include <random>

namespace livekit_bridge {
namespace test {

static std::vector<std::uint8_t> randomPayload(size_t size) {
  static thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<int> dist(0, 255);
  std::vector<std::uint8_t> data(size);
  for (auto &b : data)
    b = static_cast<std::uint8_t>(dist(gen));
  return data;
}

class BridgeDataStressTest : public BridgeTestBase {};

// ---------------------------------------------------------------------------
// High-throughput data track stress test.
//
// Pushes data frames as fast as possible for STRESS_DURATION_SECONDS and
// tracks throughput, delivery rate, and back-pressure failures.
// ---------------------------------------------------------------------------
TEST_F(BridgeDataStressTest, HighThroughput) {
  skipIfNotConfigured();

  constexpr size_t kPayloadSize = 1024;

  std::cout << "\n=== Bridge Data High-Throughput Stress Test ===" << std::endl;
  std::cout << "Duration:      " << config_.stress_duration_seconds << "s"
            << std::endl;
  std::cout << "Payload size:  " << kPayloadSize << " bytes" << std::endl;

  LiveKitBridge caller;
  LiveKitBridge receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  const std::string track_name = "throughput-data";
  const std::string caller_identity = "rpc-caller";

  auto data_track = caller.createDataTrack(track_name);
  ASSERT_NE(data_track, nullptr);

  StressTestStats stats;
  std::atomic<int64_t> frames_received{0};
  std::atomic<bool> running{true};

  receiver.setOnDataFrameCallback(
      caller_identity, track_name,
      [&](const std::vector<std::uint8_t> &payload,
          std::optional<std::uint64_t>) {
        frames_received++;
        (void)payload;
      });

  std::this_thread::sleep_for(3s);

  auto start_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::seconds(config_.stress_duration_seconds);

  std::thread sender([&]() {
    while (running.load()) {
      auto payload = randomPayload(kPayloadSize);

      auto t0 = std::chrono::high_resolution_clock::now();
      bool ok = data_track->pushFrame(payload);
      auto t1 = std::chrono::high_resolution_clock::now();

      double latency_ms =
          std::chrono::duration<double, std::milli>(t1 - t0).count();

      if (ok) {
        stats.recordCall(true, latency_ms, kPayloadSize);
      } else {
        stats.recordCall(false, latency_ms, kPayloadSize);
        stats.recordError("push_failed");
      }

      std::this_thread::sleep_for(5ms);
    }
  });

  std::thread progress([&]() {
    int last_total = 0;
    while (running.load()) {
      std::this_thread::sleep_for(30s);
      if (!running.load())
        break;

      auto elapsed = std::chrono::steady_clock::now() - start_time;
      auto elapsed_s =
          std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
      int cur_total = stats.totalCalls();
      int rate = (cur_total - last_total);
      last_total = cur_total;

      std::cout << "[" << elapsed_s << "s]"
                << " sent=" << cur_total
                << " recv=" << frames_received.load()
                << " success=" << stats.successfulCalls()
                << " failed=" << stats.failedCalls()
                << " rate=" << std::fixed << std::setprecision(1)
                << (rate / 30.0) << " pushes/s" << std::endl;
    }
  });

  while (std::chrono::steady_clock::now() - start_time < duration) {
    std::this_thread::sleep_for(1s);
  }

  running.store(false);
  sender.join();
  progress.join();

  std::this_thread::sleep_for(2s);

  stats.printStats("Bridge Data High-Throughput Stress");

  std::cout << "Frames received: " << frames_received.load() << std::endl;

  EXPECT_GT(stats.successfulCalls(), 0) << "No successful pushes";
  double success_rate =
      stats.totalCalls() > 0
          ? (100.0 * stats.successfulCalls() / stats.totalCalls())
          : 0.0;
  EXPECT_GT(success_rate, 95.0) << "Push success rate below 95%";

  double delivery_rate =
      stats.successfulCalls() > 0
          ? (100.0 * frames_received.load() / stats.successfulCalls())
          : 0.0;
  std::cout << "Delivery rate: " << std::fixed << std::setprecision(2)
            << delivery_rate << "%" << std::endl;

  receiver.clearOnDataFrameCallback(caller_identity, track_name);
}

// ---------------------------------------------------------------------------
// Large payload stress: pushes 64KB payloads for the configured duration.
// Exercises serialization / deserialization with larger frames.
// ---------------------------------------------------------------------------
TEST_F(BridgeDataStressTest, LargePayloadStress) {
  skipIfNotConfigured();

  constexpr size_t kLargePayloadSize = 64 * 1024;

  std::cout << "\n=== Bridge Data Large-Payload Stress Test ===" << std::endl;
  std::cout << "Duration:      " << config_.stress_duration_seconds << "s"
            << std::endl;
  std::cout << "Payload size:  " << kLargePayloadSize << " bytes (64KB)"
            << std::endl;

  LiveKitBridge caller;
  LiveKitBridge receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  const std::string track_name = "large-data";
  const std::string caller_identity = "rpc-caller";

  auto data_track = caller.createDataTrack(track_name);
  ASSERT_NE(data_track, nullptr);

  StressTestStats stats;
  std::atomic<int64_t> frames_received{0};
  std::atomic<int64_t> bytes_received{0};
  std::atomic<bool> running{true};

  receiver.setOnDataFrameCallback(
      caller_identity, track_name,
      [&](const std::vector<std::uint8_t> &payload,
          std::optional<std::uint64_t>) {
        frames_received++;
        bytes_received += payload.size();
      });

  std::this_thread::sleep_for(3s);

  auto start_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::seconds(config_.stress_duration_seconds);

  std::thread sender([&]() {
    while (running.load()) {
      auto payload = randomPayload(kLargePayloadSize);

      auto t0 = std::chrono::high_resolution_clock::now();
      bool ok = data_track->pushFrame(payload);
      auto t1 = std::chrono::high_resolution_clock::now();

      double latency_ms =
          std::chrono::duration<double, std::milli>(t1 - t0).count();

      if (ok) {
        stats.recordCall(true, latency_ms, kLargePayloadSize);
      } else {
        stats.recordCall(false, latency_ms, kLargePayloadSize);
        stats.recordError("push_failed");
      }

      std::this_thread::sleep_for(50ms);
    }
  });

  std::thread progress([&]() {
    int last_total = 0;
    while (running.load()) {
      std::this_thread::sleep_for(30s);
      if (!running.load())
        break;

      auto elapsed = std::chrono::steady_clock::now() - start_time;
      auto elapsed_s =
          std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
      int cur_total = stats.totalCalls();

      std::cout << "[" << elapsed_s << "s]"
                << " sent=" << cur_total
                << " recv=" << frames_received.load()
                << " bytes_rx=" << (bytes_received.load() / (1024.0 * 1024.0))
                << " MB"
                << " rate=" << std::fixed << std::setprecision(1)
                << ((cur_total - last_total) / 30.0) << " pushes/s"
                << std::endl;
      last_total = cur_total;
    }
  });

  while (std::chrono::steady_clock::now() - start_time < duration) {
    std::this_thread::sleep_for(1s);
  }

  running.store(false);
  sender.join();
  progress.join();

  std::this_thread::sleep_for(2s);

  stats.printStats("Bridge Data Large-Payload Stress");

  std::cout << "Frames received: " << frames_received.load() << std::endl;
  std::cout << "Bytes received:  "
            << (bytes_received.load() / (1024.0 * 1024.0)) << " MB"
            << std::endl;

  EXPECT_GT(stats.successfulCalls(), 0) << "No successful pushes";
  double success_rate =
      stats.totalCalls() > 0
          ? (100.0 * stats.successfulCalls() / stats.totalCalls())
          : 0.0;
  EXPECT_GT(success_rate, 90.0) << "Push success rate below 90%";

  receiver.clearOnDataFrameCallback(caller_identity, track_name);
}

// ---------------------------------------------------------------------------
// Callback churn: rapidly register/unregister the data frame callback while
// the sender is actively pushing. Exercises the bridge's thread-joining
// logic under contention.
// ---------------------------------------------------------------------------
TEST_F(BridgeDataStressTest, CallbackChurn) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Data Callback Churn Stress Test ===" << std::endl;

  LiveKitBridge caller;
  LiveKitBridge receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  const std::string track_name = "churn-data";
  const std::string caller_identity = "rpc-caller";

  auto data_track = caller.createDataTrack(track_name);
  ASSERT_NE(data_track, nullptr);

  std::atomic<bool> running{true};
  std::atomic<int64_t> total_received{0};
  std::atomic<int> churn_cycles{0};

  std::thread sender([&]() {
    while (running.load()) {
      auto payload = randomPayload(256);
      data_track->pushFrame(payload);
      std::this_thread::sleep_for(10ms);
    }
  });

  std::thread churner([&]() {
    while (running.load()) {
      receiver.setOnDataFrameCallback(
          caller_identity, track_name,
          [&](const std::vector<std::uint8_t> &,
              std::optional<std::uint64_t>) { total_received++; });

      std::this_thread::sleep_for(500ms);

      receiver.clearOnDataFrameCallback(caller_identity, track_name);

      std::this_thread::sleep_for(200ms);
      churn_cycles++;
    }
  });

  const int churn_duration_s = std::min(config_.stress_duration_seconds, 30);
  std::this_thread::sleep_for(std::chrono::seconds(churn_duration_s));

  running.store(false);
  sender.join();
  churner.join();

  std::cout << "Churn cycles completed: " << churn_cycles.load() << std::endl;
  std::cout << "Total frames received:  " << total_received.load()
            << std::endl;

  EXPECT_GT(churn_cycles.load(), 0)
      << "Should have completed at least one churn cycle";
}

} // namespace test
} // namespace livekit_bridge
