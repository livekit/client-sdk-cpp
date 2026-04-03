/*
 * Copyright 2024 LiveKit
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

#include <livekit/tracing.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>

namespace livekit {
namespace test {
namespace benchmark {

// =============================================================================
// Trace Categories (for documentation/organization)
// =============================================================================

constexpr const char *kCategoryConnect = "livekit.benchmark.connect";
constexpr const char *kCategoryRpc = "livekit.benchmark.rpc";
constexpr const char *kCategoryAudio = "livekit.benchmark.audio";
constexpr const char *kCategoryDataChannel = "livekit.benchmark.datachannel";

// =============================================================================
// Trace Event Structure (for parsing trace files)
// =============================================================================

/**
 * Represents a single trace event parsed from a trace file.
 */
struct TraceEvent {
  char phase = 0; // 'B' = begin, 'E' = end, 'S' = async start, 'F' = async
                  // finish, 'I' = instant
  std::string category;      // Event category
  std::string name;          // Event name
  uint64_t id = 0;           // Event ID (for async event pairing)
  uint64_t timestamp_us = 0; // Timestamp in microseconds
  uint32_t pid = 0;          // Process ID
  uint32_t tid = 0;          // Thread ID

  // Arguments (optional)
  std::map<std::string, double> double_args;
  std::map<std::string, std::string> string_args;
};

// =============================================================================
// Benchmark Statistics
// =============================================================================

/**
 * Statistics calculated from trace event durations.
 */
struct BenchmarkStats {
  std::string name;
  size_t count = 0;
  double min_ms = 0.0;
  double max_ms = 0.0;
  double avg_ms = 0.0;
  double p50_ms = 0.0;
  double p95_ms = 0.0;
  double p99_ms = 0.0;
};

// =============================================================================
// Trace File Analysis
// =============================================================================

/**
 * Load and parse a trace file in Chrome trace format.
 *
 * @param trace_file_path Path to the trace JSON file
 * @return Vector of parsed trace events, empty if file cannot be read
 */
std::vector<TraceEvent> loadTraceFile(const std::string &trace_file_path);

/**
 * Calculate durations for paired begin/end or async start/finish events.
 *
 * Pairs events by:
 *   - 'B'/'E' (scoped events): matched by name and thread ID
 *   - 'S'/'F' (async events): matched by name and event ID
 *
 * @param events Vector of trace events
 * @param name Event name to match (category is ignored, matches by name only)
 * @return Vector of durations in milliseconds
 */
std::vector<double> calculateDurations(const std::vector<TraceEvent> &events,
                                       const std::string &name);

/**
 * Calculate statistics from duration measurements.
 *
 * @param name Name for the statistics (for display)
 * @param durations Vector of duration measurements in milliseconds
 * @return Calculated statistics
 */
BenchmarkStats calculateStats(const std::string &name,
                              const std::vector<double> &durations);

/**
 * Analyze a trace file and calculate statistics for a specific event.
 *
 * This is a convenience function that combines loadTraceFile(),
 * calculateDurations(), and calculateStats().
 *
 * @param trace_file_path Path to the trace JSON file
 * @param event_name Event name to analyze
 * @return Statistics for the event, or empty stats if not found
 */
BenchmarkStats analyzeTraceFile(const std::string &trace_file_path,
                                const std::string &event_name);

/**
 * Analyze a trace file and calculate statistics for multiple events.
 *
 * @param trace_file_path Path to the trace JSON file
 * @param event_names List of event names to analyze
 * @return Map of event name to statistics
 */
std::map<std::string, BenchmarkStats>
analyzeTraceFile(const std::string &trace_file_path,
                 const std::vector<std::string> &event_names);

// =============================================================================
// Output Functions
// =============================================================================

/**
 * Print benchmark statistics to stdout.
 *
 * @param stats Statistics to print
 */
void printStats(const BenchmarkStats &stats);

/**
 * Print a comparison table of multiple benchmark results.
 *
 * @param results Vector of benchmark statistics to compare
 */
void printComparisonTable(const std::vector<BenchmarkStats> &results);

/**
 * Export benchmark results to CSV format.
 *
 * @param results Vector of benchmark statistics
 * @return CSV formatted string
 */
std::string exportToCsv(const std::vector<BenchmarkStats> &results);

// =============================================================================
// Benchmark Session (wraps tracing lifecycle)
// =============================================================================

/**
 * Manages a benchmark session with automatic tracing.
 *
 * Usage:
 *   BenchmarkSession session("MyBenchmark", {"livekit.*"});
 *   session.start("my_trace.json");
 *   // ... run benchmark ...
 *   session.stop();
 */
class BenchmarkSession {
public:
  BenchmarkSession(const std::string &name,
                   const std::vector<std::string> &categories = {})
      : name_(name), categories_(categories) {
    if (categories_.empty()) {
      categories_ = {"livekit", "livekit.*"};
    }
  }

  ~BenchmarkSession() {
    if (running_) {
      stop();
    }
  }

  void start(const std::string &trace_filename = "") {
    if (running_)
      return;
    trace_filename_ =
        trace_filename.empty() ? (name_ + "_trace.json") : trace_filename;
    livekit::startTracing(trace_filename_, categories_);
    running_ = true;
    std::cout << "\n=== Benchmark Session Started: " << name_
              << " ===" << std::endl;
  }

  void stop() {
    if (!running_)
      return;
    livekit::stopTracing();
    running_ = false;
    std::cout << "=== Benchmark Session Stopped: " << name_
              << " ===" << std::endl;
    std::cout << "Trace saved to: " << trace_filename_ << std::endl;
    std::cout << "View in Chrome: chrome://tracing or https://ui.perfetto.dev"
              << std::endl;
  }

  bool isRunning() const { return running_; }
  const std::string &name() const { return name_; }
  const std::string &traceFilename() const { return trace_filename_; }

private:
  std::string name_;
  std::string trace_filename_;
  std::vector<std::string> categories_;
  bool running_ = false;
};

// =============================================================================
// Stress Test Statistics (in-memory, no tracing dependency)
// =============================================================================

/**
 * Thread-safe statistics collector for stress tests.
 * Tracks success/failure counts, latencies, and error breakdown.
 */
class TracedStressStats {
public:
  TracedStressStats(const std::string &name,
                    const char *category = kCategoryRpc)
      : name_(name), category_(category) {}

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

  BenchmarkStats getLatencyStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return calculateStats(name_ + " Latency", latencies_);
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

  size_t totalBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_bytes_;
  }

  double successRate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_calls_ > 0 ? (100.0 * successful_calls_ / total_calls_) : 0.0;
  }

  std::map<std::string, int> errorCounts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_counts_;
  }

  void printStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << "\n========================================" << std::endl;
    std::cout << "  " << name_ << std::endl;
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
      std::vector<double> sorted = latencies_;
      std::sort(sorted.begin(), sorted.end());

      double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
      double avg = sum / sorted.size();

      auto percentile = [&sorted](double p) {
        size_t idx = static_cast<size_t>(sorted.size() * p / 100.0);
        if (idx >= sorted.size())
          idx = sorted.size() - 1;
        return sorted[idx];
      };

      std::cout << "\nLatency (ms):" << std::endl;
      std::cout << "  Min:    " << sorted.front() << std::endl;
      std::cout << "  Avg:    " << avg << std::endl;
      std::cout << "  P50:    " << percentile(50) << std::endl;
      std::cout << "  P95:    " << percentile(95) << std::endl;
      std::cout << "  P99:    " << percentile(99) << std::endl;
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

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    total_calls_ = 0;
    successful_calls_ = 0;
    failed_calls_ = 0;
    total_bytes_ = 0;
    latencies_.clear();
    error_counts_.clear();
  }

private:
  std::string name_;
  const char *category_;
  mutable std::mutex mutex_;

  int total_calls_ = 0;
  int successful_calls_ = 0;
  int failed_calls_ = 0;
  size_t total_bytes_ = 0;
  std::vector<double> latencies_;
  std::map<std::string, int> error_counts_;
};

} // namespace benchmark
} // namespace test
} // namespace livekit
