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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <livekit/livekit.h>
#include <map>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace livekit {
namespace test {

using namespace std::chrono_literals;

// Maximum RPC payload size (15KB)
constexpr size_t kMaxRpcPayloadSize = 15 * 1024;

// Default stress test duration in seconds (can be overridden by env var)
constexpr int kDefaultStressDurationSeconds = 600; // 10mins

// Test configuration from environment variables
struct RpcStressTestConfig {
  std::string url;
  std::string caller_token;
  std::string receiver_token;
  int duration_seconds;
  int num_caller_threads;
  bool available = false;

  static RpcStressTestConfig fromEnv() {
    RpcStressTestConfig config;
    const char *url = std::getenv("LIVEKIT_URL");
    const char *caller_token = std::getenv("LIVEKIT_CALLER_TOKEN");
    const char *receiver_token = std::getenv("LIVEKIT_RECEIVER_TOKEN");
    const char *duration_env = std::getenv("RPC_STRESS_DURATION_SECONDS");
    const char *threads_env = std::getenv("RPC_STRESS_CALLER_THREADS");

    if (url && caller_token && receiver_token) {
      config.url = url;
      config.caller_token = caller_token;
      config.receiver_token = receiver_token;
      config.available = true;
    }

    config.duration_seconds =
        duration_env ? std::atoi(duration_env) : kDefaultStressDurationSeconds;
    config.num_caller_threads = threads_env ? std::atoi(threads_env) : 4;

    return config;
  }
};

// Statistics collector
class StressTestStats {
public:
  void recordCall(bool success, double latency_ms, size_t payload_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    total_calls_++;
    if (success) {
      successful_calls_++;
      latencies_.push_back(latency_ms);
      total_bytes_ += payload_size;
    } else {
      failed_calls_++;
    }
  }

  void recordError(const std::string &error_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_counts_[error_type]++;
  }

  void printStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << "\n========================================" << std::endl;
    std::cout << "       RPC Stress Test Statistics       " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total calls:      " << total_calls_ << std::endl;
    std::cout << "Successful:       " << successful_calls_ << std::endl;
    std::cout << "Failed:           " << failed_calls_ << std::endl;
    std::cout << "Success rate:     " << std::fixed << std::setprecision(2)
              << (total_calls_ > 0 ? (100.0 * successful_calls_ / total_calls_)
                                   : 0.0)
              << "%" << std::endl;
    std::cout << "Total bytes:      " << total_bytes_ << " ("
              << (total_bytes_ / (1024.0 * 1024.0)) << " MB)" << std::endl;

    if (!latencies_.empty()) {
      std::vector<double> sorted_latencies = latencies_;
      std::sort(sorted_latencies.begin(), sorted_latencies.end());

      double sum = std::accumulate(sorted_latencies.begin(),
                                   sorted_latencies.end(), 0.0);
      double avg = sum / sorted_latencies.size();
      double min = sorted_latencies.front();
      double max = sorted_latencies.back();
      double p50 = sorted_latencies[sorted_latencies.size() * 50 / 100];
      double p95 = sorted_latencies[sorted_latencies.size() * 95 / 100];
      double p99 = sorted_latencies[sorted_latencies.size() * 99 / 100];

      std::cout << "\nLatency (ms):" << std::endl;
      std::cout << "  Min:    " << min << std::endl;
      std::cout << "  Avg:    " << avg << std::endl;
      std::cout << "  P50:    " << p50 << std::endl;
      std::cout << "  P95:    " << p95 << std::endl;
      std::cout << "  P99:    " << p99 << std::endl;
      std::cout << "  Max:    " << max << std::endl;
    }

    if (!error_counts_.empty()) {
      std::cout << "\nError breakdown:" << std::endl;
      for (const auto &pair : error_counts_) {
        std::cout << "  " << pair.first << ": " << pair.second << std::endl;
      }
    }

    std::cout << "========================================\n" << std::endl;
  }

  int totalCalls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_calls_;
  }

  int successfulCalls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return successful_calls_;
  }

  int failedCalls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_calls_;
  }

private:
  mutable std::mutex mutex_;
  int total_calls_ = 0;
  int successful_calls_ = 0;
  int failed_calls_ = 0;
  size_t total_bytes_ = 0;
  std::vector<double> latencies_;
  std::map<std::string, int> error_counts_;
};

// Sample sentences for generating compressible payloads
static const std::vector<std::string> kSampleSentences = {
    "The quick brown fox jumps over the lazy dog. ",
    "LiveKit is a real-time communication platform for building video and "
    "audio applications. ",
    "RPC allows participants to call methods on remote peers with "
    "request-response semantics. ",
    "This stress test measures the performance and reliability of the RPC "
    "system under load. ",
    "WebRTC enables peer-to-peer communication for real-time media streaming. ",
    "The payload is compressed using Zstd to reduce bandwidth and improve "
    "throughput. ",
    "Data channels provide reliable or unreliable delivery of arbitrary "
    "application data. ",
    "Participants can publish audio and video tracks to share media with "
    "others in the room. ",
    "The signaling server coordinates connection establishment between peers. ",
    "End-to-end encryption ensures that media content is only accessible to "
    "participants. ",
};

// Generate a payload of specified size using repeating sentences (compressible)
std::string generateRandomPayload(size_t size) {
  static thread_local std::random_device rd;
  static thread_local std::mt19937 gen(rd());
  static std::uniform_int_distribution<size_t> dis(0,
                                                   kSampleSentences.size() - 1);

  std::string result;
  result.reserve(size);

  // Start with a random sentence to add some variation between payloads
  size_t start_idx = dis(gen);

  while (result.size() < size) {
    // Cycle through sentences starting from a random position
    const std::string &sentence =
        kSampleSentences[(start_idx + result.size()) % kSampleSentences.size()];
    result += sentence;
  }

  // Trim to exact size
  return result.substr(0, size);
}

// Wait for a remote participant to appear
bool waitForParticipant(Room *room, const std::string &identity,
                        std::chrono::milliseconds timeout) {
  auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < timeout) {
    if (room->remoteParticipant(identity) != nullptr) {
      return true;
    }
    std::this_thread::sleep_for(100ms);
  }
  return false;
}

class RpcStressTest : public ::testing::Test {
protected:
  void SetUp() override {
    livekit::initialize(livekit::LogSink::kConsole);
    config_ = RpcStressTestConfig::fromEnv();
  }

  void TearDown() override { livekit::shutdown(); }

  RpcStressTestConfig config_;
};

// Long-running stress test with max payload sizes
TEST_F(RpcStressTest, MaxPayloadStress) {
  if (!config_.available) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                    "LIVEKIT_RECEIVER_TOKEN not set";
  }

  std::cout << "\n=== RPC Max Payload Stress Test ===" << std::endl;
  std::cout << "Duration: " << config_.duration_seconds << " seconds"
            << std::endl;
  std::cout << "Caller threads: " << config_.num_caller_threads << std::endl;
  std::cout << "Max payload size: " << kMaxRpcPayloadSize << " bytes (15KB)"
            << std::endl;

  // Create receiver room
  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  bool receiver_connected =
      receiver_room->Connect(config_.url, config_.receiver_token, options);
  ASSERT_TRUE(receiver_connected) << "Receiver failed to connect";

  std::string receiver_identity = receiver_room->localParticipant()->identity();

  std::atomic<int> total_received{0};

  // Register RPC handler that processes max payloads
  receiver_room->localParticipant()->registerRpcMethod(
      "max-payload-stress",
      [&total_received](
          const RpcInvocationData &data) -> std::optional<std::string> {
        total_received++;
        // Return checksum of payload for verification
        size_t checksum = 0;
        for (char c : data.payload) {
          checksum += static_cast<unsigned char>(c);
        }
        return std::to_string(data.payload.size()) + ":" +
               std::to_string(checksum);
      });

  // Create caller room
  auto caller_room = std::make_unique<Room>();
  bool caller_connected =
      caller_room->Connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(caller_connected) << "Caller failed to connect";

  bool receiver_visible =
      waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  std::cout << "Both rooms connected. Starting stress test..." << std::endl;

  StressTestStats stats;
  std::atomic<bool> running{true};

  auto start_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::seconds(config_.duration_seconds);

  // Create caller threads
  std::vector<std::thread> caller_threads;
  for (int t = 0; t < config_.num_caller_threads; ++t) {
    caller_threads.emplace_back([&, thread_id = t]() {
      while (running.load()) {
        // Always use max payload size (15KB)
        std::string payload = generateRandomPayload(kMaxRpcPayloadSize);

        // Calculate expected checksum
        size_t expected_checksum = 0;
        for (char c : payload) {
          expected_checksum += static_cast<unsigned char>(c);
        }

        auto call_start = std::chrono::high_resolution_clock::now();

        try {
          std::string response = caller_room->localParticipant()->performRpc(
              receiver_identity, "max-payload-stress", payload, 60.0);

          auto call_end = std::chrono::high_resolution_clock::now();
          double latency_ms =
              std::chrono::duration<double, std::milli>(call_end - call_start)
                  .count();

          // Verify response
          std::string expected_response = std::to_string(kMaxRpcPayloadSize) +
                                          ":" +
                                          std::to_string(expected_checksum);

          if (response == expected_response) {
            stats.recordCall(true, latency_ms, kMaxRpcPayloadSize);
          } else {
            stats.recordCall(false, latency_ms, kMaxRpcPayloadSize);
            stats.recordError("checksum_mismatch");
          }
        } catch (const RpcError &e) {
          auto call_end = std::chrono::high_resolution_clock::now();
          double latency_ms =
              std::chrono::duration<double, std::milli>(call_end - call_start)
                  .count();
          stats.recordCall(false, latency_ms, kMaxRpcPayloadSize);

          auto code = static_cast<RpcError::ErrorCode>(e.code());
          if (code == RpcError::ErrorCode::RESPONSE_TIMEOUT) {
            stats.recordError("timeout");
          } else if (code == RpcError::ErrorCode::CONNECTION_TIMEOUT) {
            stats.recordError("connection_timeout");
          } else if (code == RpcError::ErrorCode::RECIPIENT_DISCONNECTED) {
            stats.recordError("recipient_disconnected");
          } else {
            stats.recordError("rpc_error_" + std::to_string(e.code()));
          }
        } catch (const std::exception &e) {
          stats.recordCall(false, 0, kMaxRpcPayloadSize);
          stats.recordError("exception");
        }

        // Small delay between calls to avoid overwhelming
        std::this_thread::sleep_for(10ms);
      }
    });
  }

  // Progress reporting thread
  std::thread progress_thread([&]() {
    int last_total = 0;
    while (running.load()) {
      std::this_thread::sleep_for(30s);
      if (!running.load())
        break;

      auto elapsed = std::chrono::steady_clock::now() - start_time;
      auto elapsed_seconds =
          std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
      int current_total = stats.totalCalls();
      int calls_per_30s = current_total - last_total;
      last_total = current_total;

      std::cout << "[" << elapsed_seconds << "s] Total: " << current_total
                << " | Success: " << stats.successfulCalls()
                << " | Failed: " << stats.failedCalls()
                << " | Rate: " << (calls_per_30s / 30.0) << " calls/sec"
                << " | Received: " << total_received.load() << std::endl;
    }
  });

  // Wait for test duration
  while (std::chrono::steady_clock::now() - start_time < duration) {
    std::this_thread::sleep_for(1s);
  }

  std::cout << "\nStopping stress test..." << std::endl;
  running.store(false);

  // Wait for all threads
  for (auto &t : caller_threads) {
    t.join();
  }
  progress_thread.join();

  stats.printStats();

  // Verify results
  EXPECT_GT(stats.successfulCalls(), 0) << "No successful calls";
  double success_rate =
      (stats.totalCalls() > 0)
          ? (100.0 * stats.successfulCalls() / stats.totalCalls())
          : 0.0;
  EXPECT_GT(success_rate, 95.0) << "Success rate below 95%";

  receiver_room->localParticipant()->unregisterRpcMethod("max-payload-stress");
  caller_room.reset();
  receiver_room.reset();
}

// Stress test with varying payload sizes
TEST_F(RpcStressTest, VaryingPayloadStress) {
  if (!config_.available) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                    "LIVEKIT_RECEIVER_TOKEN not set";
  }

  std::cout << "\n=== RPC Varying Payload Stress Test ===" << std::endl;
  std::cout << "Duration: " << config_.duration_seconds << " seconds"
            << std::endl;
  std::cout << "Caller threads: " << config_.num_caller_threads << std::endl;

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  bool receiver_connected =
      receiver_room->Connect(config_.url, config_.receiver_token, options);
  ASSERT_TRUE(receiver_connected) << "Receiver failed to connect";

  std::string receiver_identity = receiver_room->localParticipant()->identity();

  std::atomic<int> total_received{0};
  std::map<size_t, std::atomic<int>> received_by_size;
  std::mutex size_map_mutex;

  receiver_room->localParticipant()->registerRpcMethod(
      "varying-payload-stress",
      [&](const RpcInvocationData &data) -> std::optional<std::string> {
        total_received++;
        size_t size = data.payload.size();

        {
          std::lock_guard<std::mutex> lock(size_map_mutex);
          received_by_size[size]++;
        }

        return std::to_string(size);
      });

  auto caller_room = std::make_unique<Room>();
  bool caller_connected =
      caller_room->Connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(caller_connected) << "Caller failed to connect";

  bool receiver_visible =
      waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  StressTestStats stats;
  std::atomic<bool> running{true};

  // Payload sizes to test
  std::vector<size_t> payload_sizes = {
      100,                    // Small
      1024,                   // 1KB
      5 * 1024,               // 5KB
      10 * 1024,              // 10KB
      kMaxRpcPayloadSize - 1, // Just under max
      kMaxRpcPayloadSize      // Max (15KB)
  };

  auto start_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::seconds(config_.duration_seconds);

  std::vector<std::thread> caller_threads;
  for (int t = 0; t < config_.num_caller_threads; ++t) {
    caller_threads.emplace_back([&, thread_id = t]() {
      int call_count = 0;
      while (running.load()) {
        size_t payload_size = payload_sizes[call_count % payload_sizes.size()];
        std::string payload = generateRandomPayload(payload_size);

        auto call_start = std::chrono::high_resolution_clock::now();

        try {
          std::string response = caller_room->localParticipant()->performRpc(
              receiver_identity, "varying-payload-stress", payload, 60.0);

          auto call_end = std::chrono::high_resolution_clock::now();
          double latency_ms =
              std::chrono::duration<double, std::milli>(call_end - call_start)
                  .count();

          if (response == std::to_string(payload_size)) {
            stats.recordCall(true, latency_ms, payload_size);
          } else {
            stats.recordCall(false, latency_ms, payload_size);
            stats.recordError("size_mismatch");
          }
        } catch (const RpcError &e) {
          stats.recordCall(false, 0, payload_size);
          auto code = static_cast<RpcError::ErrorCode>(e.code());
          if (code == RpcError::ErrorCode::RESPONSE_TIMEOUT) {
            stats.recordError("timeout");
          } else {
            stats.recordError("rpc_error");
          }
        } catch (const std::exception &) {
          stats.recordCall(false, 0, payload_size);
          stats.recordError("exception");
        }

        call_count++;
        std::this_thread::sleep_for(5ms);
      }
    });
  }

  // Progress reporting
  std::thread progress_thread([&]() {
    while (running.load()) {
      std::this_thread::sleep_for(30s);
      if (!running.load())
        break;

      auto elapsed = std::chrono::steady_clock::now() - start_time;
      auto elapsed_seconds =
          std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

      std::cout << "[" << elapsed_seconds << "s] Total: " << stats.totalCalls()
                << " | Success: " << stats.successfulCalls()
                << " | Failed: " << stats.failedCalls() << std::endl;
    }
  });

  while (std::chrono::steady_clock::now() - start_time < duration) {
    std::this_thread::sleep_for(1s);
  }

  running.store(false);

  for (auto &t : caller_threads) {
    t.join();
  }
  progress_thread.join();

  stats.printStats();

  // Print breakdown by size
  std::cout << "Received by payload size:" << std::endl;
  {
    std::lock_guard<std::mutex> lock(size_map_mutex);
    for (const auto &pair : received_by_size) {
      std::cout << "  " << pair.first << " bytes: " << pair.second.load()
                << std::endl;
    }
  }

  EXPECT_GT(stats.successfulCalls(), 0);
  double success_rate =
      (stats.totalCalls() > 0)
          ? (100.0 * stats.successfulCalls() / stats.totalCalls())
          : 0.0;
  EXPECT_GT(success_rate, 95.0) << "Success rate below 95%";

  receiver_room->localParticipant()->unregisterRpcMethod(
      "varying-payload-stress");
  caller_room.reset();
  receiver_room.reset();
}

// Stress test for bidirectional RPC (both sides can call each other)
TEST_F(RpcStressTest, BidirectionalRpcStress) {
  if (!config_.available) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                    "LIVEKIT_RECEIVER_TOKEN not set";
  }

  std::cout << "\n=== Bidirectional RPC Stress Test ===" << std::endl;
  std::cout << "Duration: " << config_.duration_seconds << " seconds"
            << std::endl;

  auto room_a = std::make_unique<Room>();
  auto room_b = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  bool a_connected =
      room_a->Connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(a_connected) << "Room A failed to connect";

  bool b_connected =
      room_b->Connect(config_.url, config_.receiver_token, options);
  ASSERT_TRUE(b_connected) << "Room B failed to connect";

  std::string identity_a = room_a->localParticipant()->identity();
  std::string identity_b = room_b->localParticipant()->identity();

  ASSERT_TRUE(waitForParticipant(room_a.get(), identity_b, 10s))
      << "Room B not visible to Room A";
  ASSERT_TRUE(waitForParticipant(room_b.get(), identity_a, 10s))
      << "Room A not visible to Room B";

  std::atomic<int> a_received{0};
  std::atomic<int> b_received{0};

  // Register handlers on both sides
  room_a->localParticipant()->registerRpcMethod(
      "ping",
      [&a_received](
          const RpcInvocationData &data) -> std::optional<std::string> {
        a_received++;
        return "pong:" + data.payload;
      });

  room_b->localParticipant()->registerRpcMethod(
      "ping",
      [&b_received](
          const RpcInvocationData &data) -> std::optional<std::string> {
        b_received++;
        return "pong:" + data.payload;
      });

  StressTestStats stats_a_to_b;
  StressTestStats stats_b_to_a;
  std::atomic<bool> running{true};

  auto start_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::seconds(config_.duration_seconds);

  // A calling B
  std::thread thread_a_to_b([&]() {
    int counter = 0;
    while (running.load()) {
      std::string payload = generateRandomPayload(kMaxRpcPayloadSize);
      auto call_start = std::chrono::high_resolution_clock::now();

      try {
        std::string response = room_a->localParticipant()->performRpc(
            identity_b, "ping", payload, 60.0);

        auto call_end = std::chrono::high_resolution_clock::now();
        double latency_ms =
            std::chrono::duration<double, std::milli>(call_end - call_start)
                .count();

        if (response == "pong:" + payload) {
          stats_a_to_b.recordCall(true, latency_ms, kMaxRpcPayloadSize);
        } else {
          stats_a_to_b.recordCall(false, latency_ms, kMaxRpcPayloadSize);
        }
      } catch (...) {
        stats_a_to_b.recordCall(false, 0, kMaxRpcPayloadSize);
      }

      counter++;
      std::this_thread::sleep_for(20ms);
    }
  });

  // B calling A
  std::thread thread_b_to_a([&]() {
    int counter = 0;
    while (running.load()) {
      std::string payload = generateRandomPayload(kMaxRpcPayloadSize);
      auto call_start = std::chrono::high_resolution_clock::now();

      try {
        std::string response = room_b->localParticipant()->performRpc(
            identity_a, "ping", payload, 60.0);

        auto call_end = std::chrono::high_resolution_clock::now();
        double latency_ms =
            std::chrono::duration<double, std::milli>(call_end - call_start)
                .count();

        if (response == "pong:" + payload) {
          stats_b_to_a.recordCall(true, latency_ms, kMaxRpcPayloadSize);
        } else {
          stats_b_to_a.recordCall(false, latency_ms, kMaxRpcPayloadSize);
        }
      } catch (...) {
        stats_b_to_a.recordCall(false, 0, kMaxRpcPayloadSize);
      }

      counter++;
      std::this_thread::sleep_for(20ms);
    }
  });

  // Progress
  std::thread progress_thread([&]() {
    while (running.load()) {
      std::this_thread::sleep_for(30s);
      if (!running.load())
        break;

      auto elapsed = std::chrono::steady_clock::now() - start_time;
      auto elapsed_seconds =
          std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

      std::cout << "[" << elapsed_seconds << "s] "
                << "A->B: " << stats_a_to_b.successfulCalls() << "/"
                << stats_a_to_b.totalCalls() << " | "
                << "B->A: " << stats_b_to_a.successfulCalls() << "/"
                << stats_b_to_a.totalCalls() << " | "
                << "A rcvd: " << a_received.load()
                << " | B rcvd: " << b_received.load() << std::endl;
    }
  });

  while (std::chrono::steady_clock::now() - start_time < duration) {
    std::this_thread::sleep_for(1s);
  }

  running.store(false);

  thread_a_to_b.join();
  thread_b_to_a.join();
  progress_thread.join();

  std::cout << "\n=== A -> B Statistics ===" << std::endl;
  stats_a_to_b.printStats();

  std::cout << "\n=== B -> A Statistics ===" << std::endl;
  stats_b_to_a.printStats();

  EXPECT_GT(stats_a_to_b.successfulCalls(), 0);
  EXPECT_GT(stats_b_to_a.successfulCalls(), 0);

  room_a->localParticipant()->unregisterRpcMethod("ping");
  room_b->localParticipant()->unregisterRpcMethod("ping");
  room_a.reset();
  room_b.reset();
}

// High throughput stress test (short bursts)
TEST_F(RpcStressTest, HighThroughputBurst) {
  if (!config_.available) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                    "LIVEKIT_RECEIVER_TOKEN not set";
  }

  std::cout << "\n=== High Throughput Burst Test ===" << std::endl;
  std::cout << "Duration: " << config_.duration_seconds << " seconds"
            << std::endl;
  std::cout << "Testing rapid-fire RPC with max payload (15KB)..." << std::endl;

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  bool receiver_connected =
      receiver_room->Connect(config_.url, config_.receiver_token, options);
  ASSERT_TRUE(receiver_connected) << "Receiver failed to connect";

  std::string receiver_identity = receiver_room->localParticipant()->identity();

  std::atomic<int> total_received{0};

  receiver_room->localParticipant()->registerRpcMethod(
      "burst-test",
      [&total_received](
          const RpcInvocationData &data) -> std::optional<std::string> {
        total_received++;
        return std::to_string(data.payload.size());
      });

  auto caller_room = std::make_unique<Room>();
  bool caller_connected =
      caller_room->Connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(caller_connected) << "Caller failed to connect";

  bool receiver_visible =
      waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  StressTestStats stats;
  std::atomic<bool> running{true};

  auto start_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::seconds(config_.duration_seconds);

  // Multiple threads sending as fast as possible
  std::vector<std::thread> burst_threads;
  for (int t = 0; t < config_.num_caller_threads * 2; ++t) {
    burst_threads.emplace_back([&]() {
      while (running.load()) {
        std::string payload = generateRandomPayload(kMaxRpcPayloadSize);

        auto call_start = std::chrono::high_resolution_clock::now();

        try {
          std::string response = caller_room->localParticipant()->performRpc(
              receiver_identity, "burst-test", payload, 60.0);

          auto call_end = std::chrono::high_resolution_clock::now();
          double latency_ms =
              std::chrono::duration<double, std::milli>(call_end - call_start)
                  .count();

          stats.recordCall(true, latency_ms, kMaxRpcPayloadSize);
        } catch (...) {
          stats.recordCall(false, 0, kMaxRpcPayloadSize);
        }

        // No delay - burst mode
      }
    });
  }

  // Progress
  std::thread progress_thread([&]() {
    int last_total = 0;
    while (running.load()) {
      std::this_thread::sleep_for(10s);
      if (!running.load())
        break;

      int current = stats.totalCalls();
      double rate = (current - last_total) / 10.0;
      last_total = current;

      auto elapsed = std::chrono::steady_clock::now() - start_time;
      auto elapsed_seconds =
          std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

      std::cout << "[" << elapsed_seconds << "s] "
                << "Total: " << current
                << " | Success: " << stats.successfulCalls()
                << " | Rate: " << rate << " calls/sec"
                << " | Throughput: " << (rate * kMaxRpcPayloadSize / 1024.0)
                << " KB/sec" << std::endl;
    }
  });

  while (std::chrono::steady_clock::now() - start_time < duration) {
    std::this_thread::sleep_for(1s);
  }

  running.store(false);

  for (auto &t : burst_threads) {
    t.join();
  }
  progress_thread.join();

  stats.printStats();

  auto total_time = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - start_time)
                        .count();
  double avg_rate = static_cast<double>(stats.totalCalls()) / total_time;
  double throughput_kbps =
      (static_cast<double>(stats.successfulCalls()) * kMaxRpcPayloadSize) /
      (total_time * 1024.0);

  std::cout << "Average rate: " << avg_rate << " calls/sec" << std::endl;
  std::cout << "Average throughput: " << throughput_kbps << " KB/sec"
            << std::endl;

  EXPECT_GT(stats.successfulCalls(), 0);

  receiver_room->localParticipant()->unregisterRpcMethod("burst-test");
  caller_room.reset();
  receiver_room.reset();
}

} // namespace test
} // namespace livekit
