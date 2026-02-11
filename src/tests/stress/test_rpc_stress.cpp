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

#include "../common/test_common.h"
#include <fstream>
#include <random>
#include <sstream>

namespace livekit {
namespace test {

// Maximum RPC payload size (15KB)
constexpr size_t kMaxRpcPayloadSize = 15 * 1024;

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

class RpcStressTest : public LiveKitTestBase {};

// Long-running stress test with max payload sizes
TEST_F(RpcStressTest, MaxPayloadStress) {
  skipIfNotConfigured();

  std::cout << "\n=== RPC Max Payload Stress Test ===" << std::endl;
  std::cout << "Duration: " << config_.stress_duration_seconds << " seconds"
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
  auto duration = std::chrono::seconds(config_.stress_duration_seconds);

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

  stats.printStats("RPC Max Payload Stress Test");

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
  skipIfNotConfigured();

  // Small payload that fits in single SCTP chunk (no fragmentation overhead)
  constexpr size_t kSmallPayloadSize = 1000;

  std::cout << "\n=== RPC Small Payload Stress Test ===" << std::endl;
  std::cout << "Duration: " << config_.stress_duration_seconds << " seconds"
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

  // Register RPC handler
  receiver_room->localParticipant()->registerRpcMethod(
      "small-payload-stress",
      [&total_received](
          const RpcInvocationData &data) -> std::optional<std::string> {
        total_received++;
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
  auto duration = std::chrono::seconds(config_.stress_duration_seconds);

  // Create caller threads
  std::vector<std::thread> caller_threads;
  for (int t = 0; t < config_.num_caller_threads; ++t) {
    caller_threads.emplace_back([&, thread_id = t]() {
      while (running.load()) {
        std::string payload = generateRandomPayload(kSmallPayloadSize);

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
          } else if (code == RpcError::ErrorCode::RECIPIENT_DISCONNECTED) {
            stats.recordError("recipient_disconnected");
          } else {
            stats.recordError("rpc_error_" + std::to_string(e.code()));
          }
        } catch (const std::exception &e) {
          stats.recordCall(false, 0, kSmallPayloadSize);
          stats.recordError("exception");
        }

        // Minimal delay for small payloads
        std::this_thread::sleep_for(5ms);
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

  stats.printStats("RPC Small Payload Stress Test");

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

} // namespace test
} // namespace livekit
