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
#include <fstream>
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
constexpr int kDefaultStressDurationSeconds = 600; // 10 minutes

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

// Path to test data file (relative to repo root)
static const char *kTestDataFile = "data/rpc_test_data.txt";

// Loaded test data lines
static std::vector<std::string> gTestDataLines;
static std::once_flag gTestDataLoadFlag;

// Load test data from file
void loadTestData() {
  std::call_once(gTestDataLoadFlag, []() {
    // Try to find the data file relative to different possible working
    // directories
    std::vector<std::string> search_paths = {
        kTestDataFile,
        std::string("../") + kTestDataFile,
        std::string("../../") + kTestDataFile,
        std::string("../../../") + kTestDataFile,
    };

    std::ifstream file;
    for (const auto &path : search_paths) {
      file.open(path);
      if (file.is_open()) {
        std::cout << "Loaded test data from: " << path << std::endl;
        break;
      }
    }

    if (!file.is_open()) {
      std::cerr << "Warning: Could not find " << kTestDataFile
                << ", using fallback test data" << std::endl;
      // Fallback to some default lines if file not found
      gTestDataLines = {
          "This is a fallback test line for RPC stress testing.",
          "The test data file could not be found in the expected location.",
          "Please ensure data/rpc_test_data.txt exists in the repository.",
      };
      return;
    }

    std::string line;
    while (std::getline(file, line)) {
      if (!line.empty()) {
        gTestDataLines.push_back(line);
      }
    }
    file.close();

    std::cout << "Loaded " << gTestDataLines.size() << " lines of test data"
              << std::endl;
  });
}

// Truncate a string at a valid UTF-8 boundary, then pad with spaces to exact
// size
std::string truncateUtf8AndPad(const std::string &str, size_t target_size) {
  if (str.size() <= target_size) {
    // Pad with spaces to reach target size
    std::string result = str;
    result.append(target_size - str.size(), ' ');
    return result;
  }

  // Find the last valid UTF-8 character boundary before target_size
  size_t pos = target_size;
  while (pos > 0) {
    unsigned char c = static_cast<unsigned char>(str[pos]);
    // UTF-8 continuation bytes start with 10xxxxxx (0x80-0xBF)
    // If we're at a continuation byte, move back
    if ((c & 0xC0) != 0x80) {
      // This is either ASCII or the start of a multi-byte sequence
      break;
    }
    pos--;
  }

  std::string result = str.substr(0, pos);
  // Pad with spaces to reach exact target size
  if (result.size() < target_size) {
    result.append(target_size - result.size(), ' ');
  }
  return result;
}

// Generate a payload of specified size using random lines from test data
// This produces realistic text that doesn't compress as artificially well as
// repeated sentences
std::string generateRandomPayload(size_t size) {
  loadTestData();

  static thread_local std::random_device rd;
  static thread_local std::mt19937 gen(rd());

  if (gTestDataLines.empty()) {
    // Should not happen, but return empty string if no data
    return std::string(size, 'x');
  }

  std::uniform_int_distribution<size_t> dis(0, gTestDataLines.size() - 1);

  std::string result;
  result.reserve(size + 100); // Extra space for potential truncation

  // Build payload from random lines
  while (result.size() < size) {
    size_t line_idx = dis(gen);
    const std::string &line = gTestDataLines[line_idx];
    if (!result.empty()) {
      result += "\n";
    }
    result += line;
  }

  // Truncate at valid UTF-8 boundary and pad to exact size
  return truncateUtf8AndPad(result, size);
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

  auto receiver_info = receiver_room->room_info();
  std::cout << "Receiver connected - Room: " << receiver_info.name
            << " (SID: " << receiver_info.sid.value_or("unknown") << ")"
            << std::endl;

  std::string receiver_identity = receiver_room->localParticipant()->identity();

  std::atomic<int> total_received{0};

  // Register RPC handler that processes max payloads
  receiver_room->localParticipant()->registerRpcMethod(
      "max-payload-stress",
      [&total_received](
          const RpcInvocationData &data) -> std::optional<std::string> {
        total_received++;
        // Echo the payload back for round-trip verification
        return data.payload;
      });

  // Create caller room
  auto caller_room = std::make_unique<Room>();
  bool caller_connected =
      caller_room->Connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(caller_connected) << "Caller failed to connect";

  auto caller_info = caller_room->room_info();
  std::cout << "Caller connected - Room: " << caller_info.name
            << " (SID: " << caller_info.sid.value_or("unknown") << ")"
            << std::endl;

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

          // Verify response by comparing checksum (more efficient than full
          // comparison)
          size_t response_checksum = 0;
          for (char c : response) {
            response_checksum += static_cast<unsigned char>(c);
          }

          if (response.size() == payload.size() &&
              response_checksum == expected_checksum) {
            stats.recordCall(true, latency_ms, kMaxRpcPayloadSize);
          } else {
            stats.recordCall(false, latency_ms, kMaxRpcPayloadSize);
            stats.recordError("checksum_mismatch");
            std::cerr << "[CHECKSUM MISMATCH] sent size=" << payload.size()
                      << " checksum=" << expected_checksum
                      << " | received size=" << response.size()
                      << " checksum=" << response_checksum << std::endl;
          }
        } catch (const RpcError &e) {
          auto call_end = std::chrono::high_resolution_clock::now();
          double latency_ms =
              std::chrono::duration<double, std::milli>(call_end - call_start)
                  .count();
          stats.recordCall(false, latency_ms, kMaxRpcPayloadSize);

          auto code = static_cast<RpcError::ErrorCode>(e.code());
          std::cerr << "[RPC ERROR] code=" << e.code() << " message=\""
                    << e.message() << "\""
                    << " data=\"" << e.data() << "\""
                    << " latency=" << latency_ms << "ms" << std::endl;

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
          std::cerr << "[EXCEPTION] " << e.what() << std::endl;
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

// Small payload stress test - fits in single SCTP chunk (no fragmentation)
// SCTP MTU is ~1200 bytes, so we use 1000 bytes to leave room for headers
TEST_F(RpcStressTest, SmallPayloadStress) {
  if (!config_.available) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                    "LIVEKIT_RECEIVER_TOKEN not set";
  }

  // Small payload that fits in single SCTP chunk (no fragmentation overhead)
  constexpr size_t kSmallPayloadSize = 1000;

  std::cout << "\n=== RPC Small Payload Stress Test ===" << std::endl;
  std::cout << "Duration: " << config_.duration_seconds << " seconds"
            << std::endl;
  std::cout << "Caller threads: " << config_.num_caller_threads << std::endl;
  std::cout << "Payload size: " << kSmallPayloadSize
            << " bytes (single SCTP chunk)" << std::endl;

  // Create receiver room
  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  bool receiver_connected =
      receiver_room->Connect(config_.url, config_.receiver_token, options);
  ASSERT_TRUE(receiver_connected) << "Receiver failed to connect";

  auto receiver_info = receiver_room->room_info();
  std::cout << "Receiver connected - Room: " << receiver_info.name
            << " (SID: " << receiver_info.sid.value_or("unknown") << ")"
            << std::endl;

  std::string receiver_identity = receiver_room->localParticipant()->identity();

  std::atomic<int> total_received{0};

  // Register RPC handler that echoes small payloads
  receiver_room->localParticipant()->registerRpcMethod(
      "small-payload-stress",
      [&total_received](
          const RpcInvocationData &data) -> std::optional<std::string> {
        total_received++;
        // Echo the payload back for round-trip verification
        return data.payload;
      });

  // Create caller room
  auto caller_room = std::make_unique<Room>();
  bool caller_connected =
      caller_room->Connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(caller_connected) << "Caller failed to connect";

  auto caller_info = caller_room->room_info();
  std::cout << "Caller connected - Room: " << caller_info.name
            << " (SID: " << caller_info.sid.value_or("unknown") << ")"
            << std::endl;

  bool receiver_visible =
      waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  std::cout << "Both rooms connected. Starting small payload stress test..."
            << std::endl;

  StressTestStats stats;
  std::atomic<bool> running{true};

  auto start_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::seconds(config_.duration_seconds);

  // Create caller threads
  std::vector<std::thread> caller_threads;
  for (int t = 0; t < config_.num_caller_threads; ++t) {
    caller_threads.emplace_back([&, thread_id = t]() {
      while (running.load()) {
        // Use small payload that fits in single SCTP chunk
        std::string payload = generateRandomPayload(kSmallPayloadSize);

        // Calculate expected checksum
        size_t expected_checksum = 0;
        for (char c : payload) {
          expected_checksum += static_cast<unsigned char>(c);
        }

        auto call_start = std::chrono::high_resolution_clock::now();

        try {
          std::string response = caller_room->localParticipant()->performRpc(
              receiver_identity, "small-payload-stress", payload, 60.0);

          auto call_end = std::chrono::high_resolution_clock::now();
          double latency_ms =
              std::chrono::duration<double, std::milli>(call_end - call_start)
                  .count();

          // Verify response by comparing checksum
          size_t response_checksum = 0;
          for (char c : response) {
            response_checksum += static_cast<unsigned char>(c);
          }

          if (response.size() == payload.size() &&
              response_checksum == expected_checksum) {
            stats.recordCall(true, latency_ms, kSmallPayloadSize);
          } else {
            stats.recordCall(false, latency_ms, kSmallPayloadSize);
            stats.recordError("checksum_mismatch");
          }
        } catch (const RpcError &e) {
          auto call_end = std::chrono::high_resolution_clock::now();
          double latency_ms =
              std::chrono::duration<double, std::milli>(call_end - call_start)
                  .count();
          stats.recordCall(false, latency_ms, kSmallPayloadSize);

          auto code = static_cast<RpcError::ErrorCode>(e.code());
          if (code == RpcError::ErrorCode::RESPONSE_TIMEOUT) {
            stats.recordError("timeout");
          } else if (code == RpcError::ErrorCode::CONNECTION_TIMEOUT) {
            stats.recordError("connection_timeout");
          } else {
            stats.recordError("rpc_error_" + std::to_string(e.code()));
          }
        } catch (const std::exception &e) {
          stats.recordCall(false, 0, kSmallPayloadSize);
          stats.recordError("exception");
        }

        // Small delay between calls
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

  receiver_room->localParticipant()->unregisterRpcMethod(
      "small-payload-stress");
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

  // Response sizes to use (varying)
  // Note: Leave room for metadata prefix "request_size:response_size:checksum:"
  // which is about 25 bytes max
  constexpr size_t kMetadataOverhead = 30;
  std::vector<size_t> response_sizes = {
      100,                                   // Small (no compression)
      1024,                                  // 1KB (compression threshold)
      5 * 1024,                              // 5KB
      10 * 1024,                             // 10KB
      kMaxRpcPayloadSize - kMetadataOverhead // Max minus metadata overhead
  };

  receiver_room->localParticipant()->registerRpcMethod(
      "varying-payload-stress",
      [&, response_sizes](
          const RpcInvocationData &data) -> std::optional<std::string> {
        total_received++;
        size_t request_size = data.payload.size();

        {
          std::lock_guard<std::mutex> lock(size_map_mutex);
          received_by_size[request_size]++;
        }

        // Generate a random response payload of varying size
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dis(0, response_sizes.size() - 1);
        size_t response_size = response_sizes[dis(gen)];

        std::string response_payload = generateRandomPayload(response_size);

        // Calculate checksum for verification
        size_t checksum = 0;
        for (char c : response_payload) {
          checksum += static_cast<unsigned char>(c);
        }

        // Return format: "request_size:response_size:checksum:payload"
        // This allows sender to verify both request was received and response
        // is correct
        return std::to_string(request_size) + ":" +
               std::to_string(response_size) + ":" + std::to_string(checksum) +
               ":" + response_payload;
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

          // Parse response format:
          // "request_size:response_size:checksum:payload"
          bool valid = false;
          size_t first_colon = response.find(':');
          size_t second_colon = response.find(':', first_colon + 1);
          size_t third_colon = response.find(':', second_colon + 1);

          if (first_colon != std::string::npos &&
              second_colon != std::string::npos &&
              third_colon != std::string::npos) {
            size_t recv_request_size =
                std::stoull(response.substr(0, first_colon));
            size_t recv_response_size = std::stoull(response.substr(
                first_colon + 1, second_colon - first_colon - 1));
            size_t recv_checksum = std::stoull(response.substr(
                second_colon + 1, third_colon - second_colon - 1));
            std::string recv_payload = response.substr(third_colon + 1);

            // Calculate actual checksum of received payload
            size_t actual_checksum = 0;
            for (char c : recv_payload) {
              actual_checksum += static_cast<unsigned char>(c);
            }

            // Verify all fields
            if (recv_request_size == payload_size &&
                recv_response_size == recv_payload.size() &&
                recv_checksum == actual_checksum) {
              valid = true;
            } else {
              std::cerr << "[VARYING MISMATCH] sent_size=" << payload_size
                        << " recv_request_size=" << recv_request_size
                        << " recv_response_size=" << recv_response_size
                        << " actual_payload_size=" << recv_payload.size()
                        << " recv_checksum=" << recv_checksum
                        << " actual_checksum=" << actual_checksum << std::endl;
            }
          } else {
            std::cerr << "[VARYING PARSE ERROR] response format invalid"
                      << std::endl;
          }

          if (valid) {
            stats.recordCall(true, latency_ms, payload_size);
          } else {
            stats.recordCall(false, latency_ms, payload_size);
            stats.recordError("verification_failed");
          }
        } catch (const RpcError &e) {
          stats.recordCall(false, 0, payload_size);
          auto code = static_cast<RpcError::ErrorCode>(e.code());
          std::cerr << "[RPC ERROR] code=" << e.code() << " message=\""
                    << e.message() << "\""
                    << " data=\"" << e.data() << "\"" << std::endl;
          if (code == RpcError::ErrorCode::RESPONSE_TIMEOUT) {
            stats.recordError("timeout");
          } else {
            stats.recordError("rpc_error");
          }
        } catch (const std::exception &ex) {
          stats.recordCall(false, 0, payload_size);
          stats.recordError("exception");
          std::cerr << "[EXCEPTION] " << ex.what() << std::endl;
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

  // Register handlers on both sides - echo payload back for verification
  room_a->localParticipant()->registerRpcMethod(
      "ping",
      [&a_received](
          const RpcInvocationData &data) -> std::optional<std::string> {
        a_received++;
        // Echo the payload back for round-trip verification
        return data.payload;
      });

  room_b->localParticipant()->registerRpcMethod(
      "ping",
      [&b_received](
          const RpcInvocationData &data) -> std::optional<std::string> {
        b_received++;
        // Echo the payload back for round-trip verification
        return data.payload;
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

      // Calculate expected checksum for verification
      size_t expected_checksum = 0;
      for (char c : payload) {
        expected_checksum += static_cast<unsigned char>(c);
      }

      auto call_start = std::chrono::high_resolution_clock::now();

      try {
        std::string response = room_a->localParticipant()->performRpc(
            identity_b, "ping", payload, 60.0);

        auto call_end = std::chrono::high_resolution_clock::now();
        double latency_ms =
            std::chrono::duration<double, std::milli>(call_end - call_start)
                .count();

        // Verify response by comparing checksum
        size_t response_checksum = 0;
        for (char c : response) {
          response_checksum += static_cast<unsigned char>(c);
        }

        if (response.size() == payload.size() &&
            response_checksum == expected_checksum) {
          stats_a_to_b.recordCall(true, latency_ms, kMaxRpcPayloadSize);
        } else {
          stats_a_to_b.recordCall(false, latency_ms, kMaxRpcPayloadSize);
          std::cerr << "[A->B MISMATCH] sent size=" << payload.size()
                    << " checksum=" << expected_checksum
                    << " | received size=" << response.size()
                    << " checksum=" << response_checksum << std::endl;
        }
      } catch (const RpcError &e) {
        stats_a_to_b.recordCall(false, 0, kMaxRpcPayloadSize);
        std::cerr << "[A->B RPC ERROR] code=" << e.code() << " message=\""
                  << e.message() << "\""
                  << " data=\"" << e.data() << "\"" << std::endl;
      } catch (const std::exception &ex) {
        stats_a_to_b.recordCall(false, 0, kMaxRpcPayloadSize);
        std::cerr << "[A->B EXCEPTION] " << ex.what() << std::endl;
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

      // Calculate expected checksum for verification
      size_t expected_checksum = 0;
      for (char c : payload) {
        expected_checksum += static_cast<unsigned char>(c);
      }

      auto call_start = std::chrono::high_resolution_clock::now();

      try {
        std::string response = room_b->localParticipant()->performRpc(
            identity_a, "ping", payload, 60.0);

        auto call_end = std::chrono::high_resolution_clock::now();
        double latency_ms =
            std::chrono::duration<double, std::milli>(call_end - call_start)
                .count();

        // Verify response by comparing checksum
        size_t response_checksum = 0;
        for (char c : response) {
          response_checksum += static_cast<unsigned char>(c);
        }

        if (response.size() == payload.size() &&
            response_checksum == expected_checksum) {
          stats_b_to_a.recordCall(true, latency_ms, kMaxRpcPayloadSize);
        } else {
          stats_b_to_a.recordCall(false, latency_ms, kMaxRpcPayloadSize);
          std::cerr << "[B->A MISMATCH] sent size=" << payload.size()
                    << " checksum=" << expected_checksum
                    << " | received size=" << response.size()
                    << " checksum=" << response_checksum << std::endl;
        }
      } catch (const RpcError &e) {
        stats_b_to_a.recordCall(false, 0, kMaxRpcPayloadSize);
        std::cerr << "[B->A RPC ERROR] code=" << e.code() << " message=\""
                  << e.message() << "\""
                  << " data=\"" << e.data() << "\"" << std::endl;
      } catch (const std::exception &ex) {
        stats_b_to_a.recordCall(false, 0, kMaxRpcPayloadSize);
        std::cerr << "[B->A EXCEPTION] " << ex.what() << std::endl;
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
        // Echo the payload back for round-trip verification
        return data.payload;
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

        // Calculate expected checksum for verification
        size_t expected_checksum = 0;
        for (char c : payload) {
          expected_checksum += static_cast<unsigned char>(c);
        }

        auto call_start = std::chrono::high_resolution_clock::now();

        try {
          std::string response = caller_room->localParticipant()->performRpc(
              receiver_identity, "burst-test", payload, 60.0);

          auto call_end = std::chrono::high_resolution_clock::now();
          double latency_ms =
              std::chrono::duration<double, std::milli>(call_end - call_start)
                  .count();

          // Verify response by comparing checksum
          size_t response_checksum = 0;
          for (char c : response) {
            response_checksum += static_cast<unsigned char>(c);
          }

          if (response.size() == payload.size() &&
              response_checksum == expected_checksum) {
            stats.recordCall(true, latency_ms, kMaxRpcPayloadSize);
          } else {
            stats.recordCall(false, latency_ms, kMaxRpcPayloadSize);
            std::cerr << "[BURST MISMATCH] sent size=" << payload.size()
                      << " checksum=" << expected_checksum
                      << " | received size=" << response.size()
                      << " checksum=" << response_checksum << std::endl;
          }
        } catch (const RpcError &e) {
          stats.recordCall(false, 0, kMaxRpcPayloadSize);
          std::cerr << "[BURST RPC ERROR] code=" << e.code() << " message=\""
                    << e.message() << "\""
                    << " data=\"" << e.data() << "\"" << std::endl;
        } catch (const std::exception &ex) {
          stats.recordCall(false, 0, kMaxRpcPayloadSize);
          std::cerr << "[BURST EXCEPTION] " << ex.what() << std::endl;
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
