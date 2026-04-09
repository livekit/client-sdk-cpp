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

#include "event_tracer.h"
#include "event_tracer_internal.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <processthreadsapi.h>
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

namespace livekit {
namespace trace {

namespace {

// Get current process ID
uint32_t GetCurrentProcessId() {
#ifdef _WIN32
  return static_cast<uint32_t>(::GetCurrentProcessId());
#else
  return static_cast<uint32_t>(getpid());
#endif
}

// Get current thread ID
uint32_t GetCurrentThreadId() {
#ifdef _WIN32
  return static_cast<uint32_t>(::GetCurrentThreadId());
#elif defined(__APPLE__)
  uint64_t tid;
  pthread_threadid_np(nullptr, &tid);
  return static_cast<uint32_t>(tid);
#else
  return static_cast<uint32_t>(pthread_self());
#endif
}

// Get timestamp in microseconds
uint64_t GetTimestampMicros() {
  auto now = std::chrono::steady_clock::now();
  auto duration = now.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(duration)
      .count();
}

// Internal trace event structure
struct TraceEventData {
  char phase;
  std::string category;
  std::string name;
  uint64_t id;
  uint64_t timestamp_us;
  uint32_t pid;
  uint32_t tid;
  unsigned char flags;
  int num_args;
  std::string arg_names[2];
  unsigned char arg_types[2];
  uint64_t arg_values[2];
  std::string arg_string_values[2];
};

// Escape a string for JSON
std::string JsonEscape(const std::string &s) {
  std::ostringstream oss;
  for (char c : s) {
    switch (c) {
    case '"':
      oss << "\\\"";
      break;
    case '\\':
      oss << "\\\\";
      break;
    case '\b':
      oss << "\\b";
      break;
    case '\f':
      oss << "\\f";
      break;
    case '\n':
      oss << "\\n";
      break;
    case '\r':
      oss << "\\r";
      break;
    case '\t':
      oss << "\\t";
      break;
    default:
      if ('\x00' <= c && c <= '\x1f') {
        oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(c);
      } else {
        oss << c;
      }
    }
  }
  return oss.str();
}

// Convert argument value to JSON based on type
std::string ArgValueToJson(unsigned char type, uint64_t value,
                           const std::string &string_value) {
  constexpr unsigned char TRACE_VALUE_TYPE_BOOL = 1;
  constexpr unsigned char TRACE_VALUE_TYPE_UINT = 2;
  constexpr unsigned char TRACE_VALUE_TYPE_INT = 3;
  constexpr unsigned char TRACE_VALUE_TYPE_DOUBLE = 4;
  constexpr unsigned char TRACE_VALUE_TYPE_POINTER = 5;
  constexpr unsigned char TRACE_VALUE_TYPE_STRING = 6;
  constexpr unsigned char TRACE_VALUE_TYPE_COPY_STRING = 7;

  std::ostringstream oss;
  switch (type) {
  case TRACE_VALUE_TYPE_BOOL:
    oss << (value ? "true" : "false");
    break;
  case TRACE_VALUE_TYPE_UINT:
    oss << value;
    break;
  case TRACE_VALUE_TYPE_INT:
    oss << static_cast<int64_t>(value);
    break;
  case TRACE_VALUE_TYPE_DOUBLE: {
    union {
      uint64_t u;
      double d;
    } converter;
    converter.u = value;
    oss << std::setprecision(17) << converter.d;
    break;
  }
  case TRACE_VALUE_TYPE_POINTER:
    oss << "\"0x" << std::hex << value << "\"";
    break;
  case TRACE_VALUE_TYPE_STRING:
  case TRACE_VALUE_TYPE_COPY_STRING:
    oss << "\"" << JsonEscape(string_value) << "\"";
    break;
  default:
    oss << value;
    break;
  }
  return oss.str();
}

// Format a single event as JSON
std::string FormatEventJson(const TraceEventData &event, uint64_t start_time) {
  std::ostringstream oss;
  oss << "{";
  oss << "\"ph\":\"" << event.phase << "\",";
  oss << "\"cat\":\"" << JsonEscape(event.category) << "\",";
  oss << "\"name\":\"" << JsonEscape(event.name) << "\",";
  oss << "\"ts\":" << (event.timestamp_us - start_time) << ",";
  oss << "\"pid\":" << event.pid << ",";
  oss << "\"tid\":" << event.tid;

  if (event.id != 0) {
    oss << ",\"id\":\"0x" << std::hex << event.id << std::dec << "\"";
  }

  if (event.num_args > 0) {
    oss << ",\"args\":{";
    for (int i = 0; i < event.num_args && i < 2; ++i) {
      if (i > 0)
        oss << ",";
      oss << "\"" << JsonEscape(event.arg_names[i]) << "\":";
      oss << ArgValueToJson(event.arg_types[i], event.arg_values[i],
                            event.arg_string_values[i]);
    }
    oss << "}";
  }

  oss << "}";
  return oss.str();
}

// Global state
std::mutex g_mutex;
std::condition_variable g_cv;
std::queue<TraceEventData> g_event_queue;
std::unordered_set<std::string> g_enabled_categories;
std::atomic<bool> g_tracing_enabled{false};
std::atomic<bool> g_shutdown_requested{false};
std::thread g_writer_thread;
std::ofstream g_trace_file;
uint64_t g_start_time = 0;
bool g_first_event = true;

// Custom tracer callbacks
GetCategoryEnabledPtr g_custom_get_category_enabled = nullptr;
AddTraceEventPtr g_custom_add_trace_event = nullptr;

// Static enabled/disabled bytes for category state
static unsigned char g_enabled_byte = 1;
static unsigned char g_disabled_byte = 0;

// Background writer thread function
void WriterThreadFunc() {
  std::vector<TraceEventData> batch;
  batch.reserve(100);

  while (true) {
    // Wait for events or shutdown
    {
      std::unique_lock<std::mutex> lock(g_mutex);
      g_cv.wait(lock, [] {
        return !g_event_queue.empty() || g_shutdown_requested.load();
      });

      // Drain the queue into a local batch
      while (!g_event_queue.empty()) {
        batch.push_back(std::move(g_event_queue.front()));
        g_event_queue.pop();
      }
    }

    // Write batch to file (outside the lock)
    for (const auto &event : batch) {
      if (g_trace_file.is_open()) {
        if (!g_first_event) {
          g_trace_file << ",";
        }
        g_first_event = false;
        g_trace_file << FormatEventJson(event, g_start_time);
      }
    }
    batch.clear();

    // Exit if shutdown requested and queue is empty
    if (g_shutdown_requested.load()) {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_event_queue.empty()) {
        break;
      }
    }
  }
}

} // namespace

void SetupEventTracer(GetCategoryEnabledPtr get_category_enabled_ptr,
                      AddTraceEventPtr add_trace_event_ptr) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_custom_get_category_enabled = get_category_enabled_ptr;
  g_custom_add_trace_event = add_trace_event_ptr;
}

const unsigned char *EventTracer::GetCategoryEnabled(const char *name) {
  // If custom tracer is set, use it
  if (g_custom_get_category_enabled) {
    return g_custom_get_category_enabled(name);
  }

  // Fast path: tracing disabled
  if (!g_tracing_enabled.load(std::memory_order_relaxed)) {
    return &g_disabled_byte;
  }

  // Check if category is enabled
  std::lock_guard<std::mutex> lock(g_mutex);

  // Empty enabled set means all categories are enabled
  if (g_enabled_categories.empty()) {
    return &g_enabled_byte;
  }

  // Check if this specific category is enabled
  std::string category_name(name);
  if (g_enabled_categories.count(category_name) > 0) {
    return &g_enabled_byte;
  }

  // Check for wildcard matches (e.g., "livekit.*" matches "livekit.connect")
  for (const auto &pattern : g_enabled_categories) {
    if (pattern.back() == '*') {
      std::string prefix = pattern.substr(0, pattern.size() - 1);
      if (category_name.compare(0, prefix.size(), prefix) == 0) {
        return &g_enabled_byte;
      }
    }
  }

  return &g_disabled_byte;
}

void EventTracer::AddTraceEvent(char phase,
                                const unsigned char *category_enabled,
                                const char *name, unsigned long long id,
                                int num_args, const char **arg_names,
                                const unsigned char *arg_types,
                                const unsigned long long *arg_values,
                                unsigned char flags) {
  // If custom tracer is set, use it
  if (g_custom_add_trace_event) {
    g_custom_add_trace_event(phase, category_enabled, name, id, num_args,
                             arg_names, arg_types, arg_values, flags);
    return;
  }

  // Skip if tracing disabled
  if (!g_tracing_enabled.load(std::memory_order_relaxed)) {
    return;
  }

  // Build the event
  TraceEventData event;
  event.phase = phase;
  event.name = name ? name : "";
  event.id = static_cast<uint64_t>(id);
  event.timestamp_us = GetTimestampMicros();
  event.pid = GetCurrentProcessId();
  event.tid = GetCurrentThreadId();
  event.flags = flags;
  event.num_args = num_args;

  // Copy arguments
  for (int i = 0; i < num_args && i < 2; ++i) {
    if (arg_names && arg_names[i]) {
      event.arg_names[i] = arg_names[i];
    }
    if (arg_types) {
      event.arg_types[i] = arg_types[i];
    }
    if (arg_values) {
      event.arg_values[i] = arg_values[i];

      // Handle string arguments
      if (arg_types && (arg_types[i] == 6 || arg_types[i] == 7)) {
        const char *str_val = reinterpret_cast<const char *>(arg_values[i]);
        if (str_val) {
          event.arg_string_values[i] = str_val;
        }
      }
    }
  }

  // Queue the event for the writer thread
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_event_queue.push(std::move(event));
  }
  g_cv.notify_one();
}

namespace internal {

bool StartTracing(const std::string &file_path,
                  const std::vector<std::string> &categories) {
  std::lock_guard<std::mutex> lock(g_mutex);

  // Don't start if already running
  if (g_tracing_enabled.load()) {
    return false;
  }

  // Open the trace file
  g_trace_file.open(file_path, std::ios::out | std::ios::trunc);
  if (!g_trace_file.is_open()) {
    return false;
  }

  // Write JSON header
  g_trace_file << "{\"traceEvents\":[";
  g_first_event = true;

  // Set start time
  g_start_time = GetTimestampMicros();

  // Set enabled categories
  g_enabled_categories.clear();
  for (const auto &cat : categories) {
    g_enabled_categories.insert(cat);
  }

  // Reset shutdown flag and start writer thread
  g_shutdown_requested.store(false);
  g_writer_thread = std::thread(WriterThreadFunc);

  // Enable tracing (must be last to ensure everything is ready)
  g_tracing_enabled.store(true, std::memory_order_release);

  return true;
}

void StopTracing() {
  // Disable tracing first to stop new events
  g_tracing_enabled.store(false, std::memory_order_release);

  // Signal writer thread to shut down
  g_shutdown_requested.store(true);
  g_cv.notify_one();

  // Wait for writer thread to finish
  if (g_writer_thread.joinable()) {
    g_writer_thread.join();
  }

  // Close the file with JSON footer
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_trace_file.is_open()) {
    g_trace_file << "],\"displayTimeUnit\":\"ms\"}";
    g_trace_file.close();
  }

  // Clear state
  g_enabled_categories.clear();
}

bool IsTracingEnabled() {
  return g_tracing_enabled.load(std::memory_order_acquire);
}

} // namespace internal

} // namespace trace
} // namespace livekit
