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

#include <gtest/gtest.h>

#include "benchmark_utils.h"
#include "event_tracer.h"
#include <livekit/tracing.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace livekit {
namespace test {
namespace {

// Helper to get cross-platform temp directory
std::string GetTempDir() {
#ifdef _WIN32
  const char *temp = std::getenv("TEMP");
  if (!temp)
    temp = std::getenv("TMP");
  if (!temp)
    temp = ".";
  return std::string(temp);
#else
  return "/tmp";
#endif
}

// Helper to generate unique temp file paths
std::string GetTempTracePath(const std::string &test_name) {
  auto now = std::chrono::steady_clock::now();
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch())
                .count();
  std::string temp_dir = GetTempDir();
#ifdef _WIN32
  return temp_dir + "\\livekit_test_trace_" + test_name + "_" +
         std::to_string(ns) + ".json";
#else
  return temp_dir + "/livekit_test_trace_" + test_name + "_" +
         std::to_string(ns) + ".json";
#endif
}

// Helper to read file contents
std::string ReadFileContents(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return "";
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// Helper to cleanup trace files
void CleanupTraceFile(const std::string &path) { std::remove(path.c_str()); }

// =============================================================================
// Basic Lifecycle Tests
// =============================================================================

TEST(TracingTest, StartAndStopTracing) {
  std::string trace_path = GetTempTracePath("start_stop");

  // Initially tracing should be disabled
  EXPECT_FALSE(isTracingEnabled());

  // Start tracing
  bool started = startTracing(trace_path);
  EXPECT_TRUE(started);
  EXPECT_TRUE(isTracingEnabled());

  // Stop tracing
  stopTracing();
  EXPECT_FALSE(isTracingEnabled());

  // Verify file was created
  std::string contents = ReadFileContents(trace_path);
  EXPECT_FALSE(contents.empty());

  // Verify file has valid JSON structure
  EXPECT_NE(contents.find("{\"traceEvents\":["), std::string::npos);
  EXPECT_NE(contents.find("],\"displayTimeUnit\":\"ms\"}"), std::string::npos);

  CleanupTraceFile(trace_path);
}

TEST(TracingTest, DoubleStartReturnsFalse) {
  std::string trace_path = GetTempTracePath("double_start");

  bool first_start = startTracing(trace_path);
  EXPECT_TRUE(first_start);

  // Second start should fail
  bool second_start = startTracing(trace_path);
  EXPECT_FALSE(second_start);

  stopTracing();
  CleanupTraceFile(trace_path);
}

TEST(TracingTest, StopWithoutStartIsSafe) {
  // Should not crash
  stopTracing();
  EXPECT_FALSE(isTracingEnabled());
}

TEST(TracingTest, InvalidPathReturnsFalse) {
  // Try to start with an invalid path
  bool started = startTracing("/nonexistent/directory/trace.json");
  EXPECT_FALSE(started);
  EXPECT_FALSE(isTracingEnabled());
}

// =============================================================================
// Event Writing Tests
// =============================================================================

TEST(TracingTest, EventsAreWrittenToFile) {
  std::string trace_path = GetTempTracePath("events_written");

  startTracing(trace_path);

  // Add some trace events using the internal API
  const unsigned char *category =
      trace::EventTracer::GetCategoryEnabled("test.category");
  EXPECT_TRUE(*category != 0); // Category should be enabled

  // Add a begin/end event pair
  trace::EventTracer::AddTraceEvent('B',          // phase: begin
                                    category,     // category_enabled
                                    "test_event", // name
                                    0,            // id
                                    0,            // num_args
                                    nullptr,      // arg_names
                                    nullptr,      // arg_types
                                    nullptr,      // arg_values
                                    0             // flags
  );

  // Small delay to ensure timestamp difference
  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  trace::EventTracer::AddTraceEvent('E',          // phase: end
                                    category,     // category_enabled
                                    "test_event", // name
                                    0,            // id
                                    0,            // num_args
                                    nullptr,      // arg_names
                                    nullptr,      // arg_types
                                    nullptr,      // arg_values
                                    0             // flags
  );

  stopTracing();

  // Verify events are in the file
  std::string contents = ReadFileContents(trace_path);
  EXPECT_NE(contents.find("\"name\":\"test_event\""), std::string::npos);
  EXPECT_NE(contents.find("\"ph\":\"B\""), std::string::npos);
  EXPECT_NE(contents.find("\"ph\":\"E\""), std::string::npos);

  CleanupTraceFile(trace_path);
}

TEST(TracingTest, AsyncEventsAreWrittenCorrectly) {
  std::string trace_path = GetTempTracePath("async_events");

  startTracing(trace_path);

  const unsigned char *category =
      trace::EventTracer::GetCategoryEnabled("test.async");

  // Add async start event
  trace::EventTracer::AddTraceEvent('S', // phase: async start
                                    category, "async_operation",
                                    12345, // id for pairing
                                    0, nullptr, nullptr, nullptr, 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  // Add async finish event
  trace::EventTracer::AddTraceEvent('F', // phase: async finish
                                    category, "async_operation",
                                    12345, // same id
                                    0, nullptr, nullptr, nullptr, 0);

  stopTracing();

  // Verify events
  std::string contents = ReadFileContents(trace_path);
  EXPECT_NE(contents.find("\"name\":\"async_operation\""), std::string::npos);
  EXPECT_NE(contents.find("\"ph\":\"S\""), std::string::npos);
  EXPECT_NE(contents.find("\"ph\":\"F\""), std::string::npos);
  EXPECT_NE(contents.find("\"id\":\"0x3039\""),
            std::string::npos); // 12345 in hex

  CleanupTraceFile(trace_path);
}

TEST(TracingTest, EventsWithArguments) {
  std::string trace_path = GetTempTracePath("events_with_args");

  startTracing(trace_path);

  const unsigned char *category =
      trace::EventTracer::GetCategoryEnabled("test.args");

  // Add event with integer argument
  const char *arg_names[] = {"count"};
  unsigned char arg_types[] = {2}; // TRACE_VALUE_TYPE_UINT = 2
  unsigned long long arg_values[] = {42};

  trace::EventTracer::AddTraceEvent('I', // instant event
                                    category, "event_with_args", 0,
                                    1, // num_args
                                    arg_names, arg_types, arg_values, 0);

  stopTracing();

  std::string contents = ReadFileContents(trace_path);
  EXPECT_NE(contents.find("\"name\":\"event_with_args\""), std::string::npos);
  EXPECT_NE(contents.find("\"count\":42"), std::string::npos);

  CleanupTraceFile(trace_path);
}

// =============================================================================
// Category Filtering Tests
// =============================================================================

TEST(TracingTest, CategoryFilteringEnablesSpecificCategory) {
  std::string trace_path = GetTempTracePath("category_filter");

  // Start with specific category enabled
  startTracing(trace_path, {"enabled.category"});

  const unsigned char *enabled_cat =
      trace::EventTracer::GetCategoryEnabled("enabled.category");
  const unsigned char *disabled_cat =
      trace::EventTracer::GetCategoryEnabled("disabled.category");

  EXPECT_TRUE(*enabled_cat != 0);
  EXPECT_TRUE(*disabled_cat == 0);

  stopTracing();
  CleanupTraceFile(trace_path);
}

TEST(TracingTest, CategoryWildcardMatching) {
  std::string trace_path = GetTempTracePath("wildcard_filter");

  // Start with wildcard category
  startTracing(trace_path, {"livekit.*"});

  const unsigned char *match1 =
      trace::EventTracer::GetCategoryEnabled("livekit.connect");
  const unsigned char *match2 =
      trace::EventTracer::GetCategoryEnabled("livekit.rpc");
  const unsigned char *no_match =
      trace::EventTracer::GetCategoryEnabled("other.category");

  EXPECT_TRUE(*match1 != 0);
  EXPECT_TRUE(*match2 != 0);
  EXPECT_TRUE(*no_match == 0);

  stopTracing();
  CleanupTraceFile(trace_path);
}

TEST(TracingTest, EmptyCategoriesEnablesAll) {
  std::string trace_path = GetTempTracePath("all_categories");

  // Start with empty categories (all enabled)
  startTracing(trace_path, {});

  const unsigned char *cat1 =
      trace::EventTracer::GetCategoryEnabled("any.category");
  const unsigned char *cat2 =
      trace::EventTracer::GetCategoryEnabled("another.category");

  EXPECT_TRUE(*cat1 != 0);
  EXPECT_TRUE(*cat2 != 0);

  stopTracing();
  CleanupTraceFile(trace_path);
}

// =============================================================================
// Trace File Analysis Tests (using benchmark_utils)
// =============================================================================

TEST(TracingTest, TraceFileCanBeParsed) {
  std::string trace_path = GetTempTracePath("parse_test");

  startTracing(trace_path);

  const unsigned char *category =
      trace::EventTracer::GetCategoryEnabled("test");

  // Add multiple events
  for (int i = 0; i < 5; ++i) {
    trace::EventTracer::AddTraceEvent('B', category, "parse_event", 0, 0,
                                      nullptr, nullptr, nullptr, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    trace::EventTracer::AddTraceEvent('E', category, "parse_event", 0, 0,
                                      nullptr, nullptr, nullptr, 0);
  }

  stopTracing();

  // Use benchmark_utils to parse the trace file
  auto events = benchmark::loadTraceFile(trace_path);
  EXPECT_FALSE(events.empty());

  // Count begin/end events for our test event
  int begin_count = 0;
  int end_count = 0;
  for (const auto &event : events) {
    if (event.name == "parse_event") {
      if (event.phase == 'B')
        begin_count++;
      if (event.phase == 'E')
        end_count++;
    }
  }

  EXPECT_EQ(begin_count, 5);
  EXPECT_EQ(end_count, 5);

  CleanupTraceFile(trace_path);
}

TEST(TracingTest, DurationCalculationWorks) {
  std::string trace_path = GetTempTracePath("duration_test");

  startTracing(trace_path);

  const unsigned char *category =
      trace::EventTracer::GetCategoryEnabled("test");

  // Add events with known delay
  for (int i = 0; i < 3; ++i) {
    trace::EventTracer::AddTraceEvent('B', category, "timed_event", 0, 0,
                                      nullptr, nullptr, nullptr, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    trace::EventTracer::AddTraceEvent('E', category, "timed_event", 0, 0,
                                      nullptr, nullptr, nullptr, 0);
  }

  stopTracing();

  // Calculate durations
  auto events = benchmark::loadTraceFile(trace_path);
  auto durations = benchmark::calculateDurations(events, "timed_event");

  EXPECT_EQ(durations.size(), 3);

  // Each duration should be at least 10ms (we slept for 10ms)
  for (double duration : durations) {
    EXPECT_GE(duration, 9.0); // Allow small timing variance
  }

  CleanupTraceFile(trace_path);
}

TEST(TracingTest, AsyncDurationCalculationWorks) {
  std::string trace_path = GetTempTracePath("async_duration_test");

  startTracing(trace_path);

  const unsigned char *category =
      trace::EventTracer::GetCategoryEnabled("test");

  // Add async events with known IDs
  trace::EventTracer::AddTraceEvent('S', category, "async_op", 100, 0, nullptr,
                                    nullptr, nullptr, 0);
  trace::EventTracer::AddTraceEvent('S', category, "async_op", 200, 0, nullptr,
                                    nullptr, nullptr, 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(15));

  trace::EventTracer::AddTraceEvent('F', category, "async_op", 100, 0, nullptr,
                                    nullptr, nullptr, 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  trace::EventTracer::AddTraceEvent('F', category, "async_op", 200, 0, nullptr,
                                    nullptr, nullptr, 0);

  stopTracing();

  auto events = benchmark::loadTraceFile(trace_path);
  auto durations = benchmark::calculateDurations(events, "async_op");

  EXPECT_EQ(durations.size(), 2);

  CleanupTraceFile(trace_path);
}

TEST(TracingTest, StatisticsCalculation) {
  std::string trace_path = GetTempTracePath("stats_test");

  startTracing(trace_path);

  const unsigned char *category =
      trace::EventTracer::GetCategoryEnabled("test");

  // Add 10 events
  for (int i = 0; i < 10; ++i) {
    trace::EventTracer::AddTraceEvent('B', category, "stats_event", 0, 0,
                                      nullptr, nullptr, nullptr, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    trace::EventTracer::AddTraceEvent('E', category, "stats_event", 0, 0,
                                      nullptr, nullptr, nullptr, 0);
  }

  stopTracing();

  // Calculate statistics
  auto stats = benchmark::analyzeTraceFile(trace_path, "stats_event");

  EXPECT_EQ(stats.count, 10);
  EXPECT_GT(stats.min_ms, 0.0);
  EXPECT_GE(stats.max_ms, stats.min_ms);
  EXPECT_GE(stats.avg_ms, stats.min_ms);
  EXPECT_LE(stats.avg_ms, stats.max_ms);

  CleanupTraceFile(trace_path);
}

// =============================================================================
// Multiple Sessions Test
// =============================================================================

TEST(TracingTest, MultipleTracingSessions) {
  std::string trace_path1 = GetTempTracePath("session1");
  std::string trace_path2 = GetTempTracePath("session2");

  // First session
  startTracing(trace_path1);
  const unsigned char *cat = trace::EventTracer::GetCategoryEnabled("test");
  trace::EventTracer::AddTraceEvent('B', cat, "session1_event", 0, 0, nullptr,
                                    nullptr, nullptr, 0);
  trace::EventTracer::AddTraceEvent('E', cat, "session1_event", 0, 0, nullptr,
                                    nullptr, nullptr, 0);
  stopTracing();

  // Second session
  startTracing(trace_path2);
  cat = trace::EventTracer::GetCategoryEnabled("test");
  trace::EventTracer::AddTraceEvent('B', cat, "session2_event", 0, 0, nullptr,
                                    nullptr, nullptr, 0);
  trace::EventTracer::AddTraceEvent('E', cat, "session2_event", 0, 0, nullptr,
                                    nullptr, nullptr, 0);
  stopTracing();

  // Verify first file contains session1 event only
  std::string contents1 = ReadFileContents(trace_path1);
  EXPECT_NE(contents1.find("session1_event"), std::string::npos);
  EXPECT_EQ(contents1.find("session2_event"), std::string::npos);

  // Verify second file contains session2 event only
  std::string contents2 = ReadFileContents(trace_path2);
  EXPECT_NE(contents2.find("session2_event"), std::string::npos);
  EXPECT_EQ(contents2.find("session1_event"), std::string::npos);

  CleanupTraceFile(trace_path1);
  CleanupTraceFile(trace_path2);
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

TEST(TracingTest, ConcurrentEventWriting) {
  std::string trace_path = GetTempTracePath("concurrent_test");

  startTracing(trace_path);

  const int num_threads = 4;
  const int events_per_thread = 100;
  std::vector<std::thread> threads;

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([t, events_per_thread]() {
      const unsigned char *cat =
          trace::EventTracer::GetCategoryEnabled("test.concurrent");
      std::string event_name = "thread_" + std::to_string(t) + "_event";

      for (int i = 0; i < events_per_thread; ++i) {
        trace::EventTracer::AddTraceEvent('B', cat, event_name.c_str(), 0, 0,
                                          nullptr, nullptr, nullptr, 0);
        trace::EventTracer::AddTraceEvent('E', cat, event_name.c_str(), 0, 0,
                                          nullptr, nullptr, nullptr, 0);
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  stopTracing();

  // Parse and count events
  auto events = benchmark::loadTraceFile(trace_path);

  // Each thread should have written events_per_thread * 2 events (begin + end)
  int total_begin = 0;
  int total_end = 0;
  for (const auto &event : events) {
    if (event.phase == 'B')
      total_begin++;
    if (event.phase == 'E')
      total_end++;
  }

  // Should have all events written
  EXPECT_EQ(total_begin, num_threads * events_per_thread);
  EXPECT_EQ(total_end, num_threads * events_per_thread);

  CleanupTraceFile(trace_path);
}

// =============================================================================
// Events Not Written When Tracing Disabled
// =============================================================================

TEST(TracingTest, EventsIgnoredWhenTracingDisabled) {
  // Ensure tracing is stopped
  stopTracing();
  EXPECT_FALSE(isTracingEnabled());

  // Get category - should return disabled
  const unsigned char *cat = trace::EventTracer::GetCategoryEnabled("test");
  EXPECT_EQ(*cat, 0);

  // Adding events should be safe (no crash)
  trace::EventTracer::AddTraceEvent('B', cat, "ignored_event", 0, 0, nullptr,
                                    nullptr, nullptr, 0);
  trace::EventTracer::AddTraceEvent('E', cat, "ignored_event", 0, 0, nullptr,
                                    nullptr, nullptr, 0);

  // Still disabled
  EXPECT_FALSE(isTracingEnabled());
}

} // namespace
} // namespace test
} // namespace livekit
