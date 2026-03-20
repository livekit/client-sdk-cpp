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

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <livekit/livekit.h>
#include <livekit_bridge/livekit_bridge.h>
#include <map>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace livekit_bridge {
namespace test {

using namespace std::chrono_literals;

constexpr int kDefaultTestIterations = 10;
constexpr int kDefaultStressDurationSeconds = 600;

/**
 * Test configuration loaded from the same environment variables used by the
 * base SDK tests (see README "RPC Test Environment Variables").
 *
 *   LIVEKIT_URL            - WebSocket URL of the LiveKit server
 *   LIVEKIT_CALLER_TOKEN   - Token for the caller/sender participant
 *   LIVEKIT_RECEIVER_TOKEN - Token for the receiver participant
 *   TEST_ITERATIONS        - Number of iterations (default: 10)
 *   STRESS_DURATION_SECONDS - Duration for stress tests (default: 600)
 */
struct BridgeTestConfig {
  std::string url;
  std::string caller_token;
  std::string receiver_token;
  int test_iterations;
  int stress_duration_seconds;
  bool available = false;

  static BridgeTestConfig fromEnv() {
    BridgeTestConfig config;
    const char *url = std::getenv("LIVEKIT_URL");
    const char *caller_token = std::getenv("LIVEKIT_CALLER_TOKEN");
    const char *receiver_token = std::getenv("LIVEKIT_RECEIVER_TOKEN");
    const char *iterations_env = std::getenv("TEST_ITERATIONS");
    const char *duration_env = std::getenv("STRESS_DURATION_SECONDS");

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

    return config;
  }
};

/**
 * Thread-safe latency statistics collector.
 * Identical to livekit::test::LatencyStats but lives in the bridge namespace
 * to avoid linking against the base SDK test helpers.
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

    std::cout << "\n========================================" << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Samples:      " << sorted.size() << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Min:          " << sorted.front() << " ms" << std::endl;
    std::cout << "Avg:          " << avg << " ms" << std::endl;
    std::cout << "P50:          " << getPercentile(sorted, 50) << " ms"
              << std::endl;
    std::cout << "P95:          " << getPercentile(sorted, 95) << " ms"
              << std::endl;
    std::cout << "P99:          " << getPercentile(sorted, 99) << " ms"
              << std::endl;
    std::cout << "Max:          " << sorted.back() << " ms" << std::endl;
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
              << (total_calls_ > 0
                      ? (100.0 * successful_calls_ / total_calls_)
                      : 0.0)
              << "%" << std::endl;
    std::cout << "Total bytes:      " << total_bytes_ << " ("
              << (total_bytes_ / (1024.0 * 1024.0)) << " MB)" << std::endl;

    if (!latencies_.empty()) {
      std::vector<double> sorted = latencies_;
      std::sort(sorted.begin(), sorted.end());

      double sum =
          std::accumulate(sorted.begin(), sorted.end(), 0.0);
      double avg = sum / sorted.size();

      std::cout << "\nLatency (ms):" << std::endl;
      std::cout << "  Min:    " << sorted.front() << std::endl;
      std::cout << "  Avg:    " << avg << std::endl;
      std::cout << "  P50:    "
                << sorted[sorted.size() * 50 / 100] << std::endl;
      std::cout << "  P95:    "
                << sorted[sorted.size() * 95 / 100] << std::endl;
      std::cout << "  P99:    "
                << sorted[sorted.size() * 99 / 100] << std::endl;
      std::cout << "  Max:    " << sorted.back() << std::endl;
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

/**
 * Base test fixture for bridge E2E tests.
 *
 * IMPORTANT — SDK lifecycle constraints:
 *
 *  • livekit::initialize() / livekit::shutdown() operate on a process-global
 *    singleton (FfiClient).  shutdown() calls livekit_ffi_dispose() which
 *    tears down the Rust runtime.  Re-initializing after a dispose can leave
 *    internal Rust state corrupt when done many times in rapid succession.
 *
 *  • Each LiveKitBridge instance independently calls initialize()/shutdown()
 *    in connect()/disconnect().  With two bridges in the same test the first
 *    one to disconnect() shuts down the SDK while the second is still alive.
 *
 * Our strategy:
 *  1.  Tests should let bridge destructors handle disconnect.  Do NOT call
 *      bridge.disconnect() at the end of a test — just let the bridge go
 *      out of scope and its destructor will disconnect and (eventually)
 *      call shutdown.
 *  2.  If you need to explicitly disconnect mid-test (e.g. to test
 *      lifecycle), accept that this triggers a shutdown.  Add a 1 s sleep
 *      after destruction before creating the next bridge so the Rust
 *      runtime fully cleans up.
 */
class BridgeTestBase : public ::testing::Test {
protected:
  void SetUp() override { config_ = BridgeTestConfig::fromEnv(); }

  void skipIfNotConfigured() {
    if (!config_.available) {
      GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                      "LIVEKIT_RECEIVER_TOKEN not set";
    }
  }

  /**
   * Connect two bridges (caller and receiver) and verify both are connected.
   * Returns false if either connection fails.
   */
  bool connectPair(LiveKitBridge &caller, LiveKitBridge &receiver) {
    livekit::RoomOptions options;
    options.auto_subscribe = true;

    bool receiver_ok =
        receiver.connect(config_.url, config_.receiver_token, options);
    if (!receiver_ok)
      return false;

    bool caller_ok =
        caller.connect(config_.url, config_.caller_token, options);
    if (!caller_ok) {
      receiver.disconnect();
      return false;
    }

    // Allow time for peer discovery
    std::this_thread::sleep_for(2s);
    return true;
  }

  BridgeTestConfig config_;
};

} // namespace test
} // namespace livekit_bridge
