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

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <livekit/livekit.h>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace livekit {
namespace test {

using namespace std::chrono_literals;

// =============================================================================
// Common Constants
// =============================================================================

// Default number of test iterations for connection tests
constexpr int kDefaultTestIterations = 10;

// Default stress test duration in seconds
constexpr int kDefaultStressDurationSeconds = 600; // 10 minutes

// Local SFU URL used by end-to-end data track tests.
constexpr char kLocalTestLiveKitUrl[] = "ws://localhost:7880";

// =============================================================================
// Common Test Configuration
// =============================================================================

/**
 * Common test configuration loaded from environment variables.
 *
 * Environment variables:
 *   LIVEKIT_URL           - WebSocket URL of the LiveKit server
 *   LIVEKIT_CALLER_TOKEN  - Token for the caller/sender participant
 *   LIVEKIT_RECEIVER_TOKEN - Token for the receiver participant
 *   TEST_ITERATIONS       - Number of iterations for iterative tests (default:
 * 10) STRESS_DURATION_SECONDS - Duration for stress tests in seconds (default:
 * 600) STRESS_CALLER_THREADS - Number of caller threads for stress tests
 * (default: 4)
 */
struct TestConfig {
  std::string url;
  std::string caller_token;
  std::string receiver_token;
  int test_iterations;
  int stress_duration_seconds;
  int num_caller_threads;
  bool available = false;

  static TestConfig fromEnv() {
    TestConfig config;
    const char *url = std::getenv("LIVEKIT_URL");
    const char *caller_token = std::getenv("LIVEKIT_CALLER_TOKEN");
    const char *receiver_token = std::getenv("LIVEKIT_RECEIVER_TOKEN");
    const char *iterations_env = std::getenv("TEST_ITERATIONS");
    const char *duration_env = std::getenv("STRESS_DURATION_SECONDS");
    const char *threads_env = std::getenv("STRESS_CALLER_THREADS");

    if (url && caller_token && receiver_token) {
      config.url = url;
      config.caller_token = caller_token;
      config.receiver_token = receiver_token;
      config.available = true;
    }

    config.test_iterations =
        iterations_env ? std::atoi(iterations_env) : kDefaultTestIterations;
    config.stress_duration_seconds =
        duration_env ? std::atoi(duration_env) : kDefaultStressDurationSeconds;
    config.num_caller_threads = threads_env ? std::atoi(threads_env) : 4;

    return config;
  }
};

struct TestRoomConnectionOptions {
  RoomOptions room_options;
  RoomDelegate *delegate = nullptr;
};

// =============================================================================
// Utility Functions
// =============================================================================

/// Get current timestamp in microseconds
inline uint64_t getTimestampUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

/// Wait for a remote participant to appear in the room
inline bool waitForParticipant(Room *room, const std::string &identity,
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

inline std::array<std::string, 2> getDataTrackTestTokens() {
  const char *token_a = std::getenv("LK_TOKEN_TEST_A");
  if (token_a == nullptr || std::string(token_a).empty()) {
    throw std::runtime_error(
        "LK_TOKEN_TEST_A must be present and non-empty for data track E2E "
        "tests");
  }

  const char *token_b = std::getenv("LK_TOKEN_TEST_B");
  if (token_b == nullptr || std::string(token_b).empty()) {
    throw std::runtime_error(
        "LK_TOKEN_TEST_B must be present and non-empty for data track E2E "
        "tests");
  }

  return {token_a, token_b};
}

inline void waitForParticipantVisibility(
    const std::vector<std::unique_ptr<Room>> &rooms,
    std::chrono::milliseconds timeout = 5s) {
  std::vector<std::string> participant_identities;
  participant_identities.reserve(rooms.size());
  for (const auto &room : rooms) {
    if (!room || room->localParticipant() == nullptr) {
      throw std::runtime_error(
          "Test room is missing a local participant after connect");
    }
    participant_identities.push_back(room->localParticipant()->identity());
  }

  auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < timeout) {
    bool all_visible = true;
    for (size_t i = 0; i < rooms.size(); ++i) {
      const auto &room = rooms[i];
      if (!room || room->localParticipant() == nullptr) {
        throw std::runtime_error(
            "Test room is missing a local participant after connect");
      }

      for (size_t j = 0; j < participant_identities.size(); ++j) {
        if (i == j) {
          continue;
        }

        if (room->remoteParticipant(participant_identities[j]) == nullptr) {
          all_visible = false;
          break;
        }
      }

      if (!all_visible) {
        break;
      }
    }

    if (all_visible) {
      return;
    }

    std::this_thread::sleep_for(10ms);
  }

  throw std::runtime_error("Not all test participants became visible");
}

inline std::vector<std::unique_ptr<Room>>
testRooms(const std::vector<TestRoomConnectionOptions> &room_configs) {
  if (room_configs.empty()) {
    throw std::invalid_argument("testRooms requires at least one room");
  }

  if (room_configs.size() > 2) {
    throw std::invalid_argument(
        "testRooms supports at most two rooms with LK_TOKEN_TEST_A/B");
  }

  auto tokens = getDataTrackTestTokens();

  std::vector<std::unique_ptr<Room>> rooms;
  rooms.reserve(room_configs.size());

  for (size_t i = 0; i < room_configs.size(); ++i) {
    auto room = std::make_unique<Room>();
    if (room_configs[i].delegate != nullptr) {
      room->setDelegate(room_configs[i].delegate);
    }

    if (!room->Connect(kLocalTestLiveKitUrl, tokens[i],
                       room_configs[i].room_options)) {
      throw std::runtime_error("Failed to connect test room " +
                               std::to_string(i));
    }

    rooms.push_back(std::move(room));
  }

  waitForParticipantVisibility(rooms);
  return rooms;
}

inline std::vector<std::unique_ptr<Room>> testRooms(size_t count) {
  std::vector<TestRoomConnectionOptions> room_configs(count);
  return testRooms(room_configs);
}

// =============================================================================
// Statistics Collection
// =============================================================================

/**
 * Thread-safe latency statistics collector.
 * Records latency measurements and provides summary statistics.
 */
class LatencyStats {
public:
  void addMeasurement(double latency_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    measurements_.push_back(latency_ms);
  }

  void printStats(const std::string &title) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (measurements_.empty()) {
      std::cout << "\n" << title << ": No measurements collected" << std::endl;
      return;
    }

    std::vector<double> sorted = measurements_;
    std::sort(sorted.begin(), sorted.end());

    double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    double avg = sum / sorted.size();
    double min = sorted.front();
    double max = sorted.back();
    double p50 = getPercentile(sorted, 50);
    double p95 = getPercentile(sorted, 95);
    double p99 = getPercentile(sorted, 99);

    std::cout << "\n========================================" << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Samples:      " << sorted.size() << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Min:          " << min << " ms" << std::endl;
    std::cout << "Avg:          " << avg << " ms" << std::endl;
    std::cout << "P50:          " << p50 << " ms" << std::endl;
    std::cout << "P95:          " << p95 << " ms" << std::endl;
    std::cout << "P99:          " << p99 << " ms" << std::endl;
    std::cout << "Max:          " << max << " ms" << std::endl;
    std::cout << "========================================\n" << std::endl;
  }

  size_t count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return measurements_.size();
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    measurements_.clear();
  }

private:
  static double getPercentile(const std::vector<double> &sorted,
                              int percentile) {
    if (sorted.empty())
      return 0.0;
    size_t index = (sorted.size() * percentile) / 100;
    if (index >= sorted.size())
      index = sorted.size() - 1;
    return sorted[index];
  }

  mutable std::mutex mutex_;
  std::vector<double> measurements_;
};

/**
 * Extended statistics collector for stress tests.
 * Tracks success/failure counts, bytes transferred, and error breakdown.
 */
class StressTestStats {
public:
  void recordCall(bool success, double latency_ms, size_t payload_size = 0) {
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

  void printStats(const std::string &title = "Stress Test Statistics") const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << "\n========================================" << std::endl;
    std::cout << "  " << title << std::endl;
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

// =============================================================================
// Base Test Fixture
// =============================================================================

/**
 * Base test fixture that handles SDK initialization and configuration loading.
 */
class LiveKitTestBase : public ::testing::Test {
protected:
  void SetUp() override {
    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);
    config_ = TestConfig::fromEnv();
  }

  void TearDown() override { livekit::shutdown(); }

  /// Skip the test if the required environment variables are not set
  void skipIfNotConfigured() {
    if (!config_.available) {
      GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                      "LIVEKIT_RECEIVER_TOKEN not set";
    }
  }

  TestConfig config_;
};

} // namespace test
} // namespace livekit
