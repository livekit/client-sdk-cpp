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

#include "benchmark_utils.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <unordered_map>

namespace livekit {
namespace test {
namespace benchmark {

namespace {

// Simple JSON parser for trace files
// Note: This is a minimal parser for Chrome trace format, not a general JSON
// parser

std::string readFile(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return "";
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// Skip whitespace
void skipWhitespace(const std::string &json, size_t &pos) {
  while (pos < json.size() && std::isspace(json[pos])) {
    pos++;
  }
}

// Parse a JSON string (assumes pos is at opening quote)
std::string parseString(const std::string &json, size_t &pos) {
  if (pos >= json.size() || json[pos] != '"') {
    return "";
  }
  pos++; // skip opening quote

  std::string result;
  while (pos < json.size() && json[pos] != '"') {
    if (json[pos] == '\\' && pos + 1 < json.size()) {
      pos++;
      switch (json[pos]) {
      case '"':
        result += '"';
        break;
      case '\\':
        result += '\\';
        break;
      case 'n':
        result += '\n';
        break;
      case 't':
        result += '\t';
        break;
      case 'r':
        result += '\r';
        break;
      default:
        result += json[pos];
        break;
      }
    } else {
      result += json[pos];
    }
    pos++;
  }
  if (pos < json.size()) {
    pos++; // skip closing quote
  }
  return result;
}

// Parse a JSON number
double parseNumber(const std::string &json, size_t &pos) {
  size_t start = pos;
  if (pos < json.size() && (json[pos] == '-' || json[pos] == '+')) {
    pos++;
  }
  while (pos < json.size() &&
         (std::isdigit(json[pos]) || json[pos] == '.' || json[pos] == 'e' ||
          json[pos] == 'E' || json[pos] == '-' || json[pos] == '+')) {
    pos++;
  }
  if (pos == start) {
    return 0.0;
  }
  try {
    return std::stod(json.substr(start, pos - start));
  } catch (...) {
    return 0.0;
  }
}

// Parse a trace event object
TraceEvent parseTraceEvent(const std::string &json, size_t &pos) {
  TraceEvent event;

  skipWhitespace(json, pos);
  if (pos >= json.size() || json[pos] != '{') {
    return event;
  }
  pos++; // skip '{'

  while (pos < json.size()) {
    skipWhitespace(json, pos);
    if (json[pos] == '}') {
      pos++;
      break;
    }
    if (json[pos] == ',') {
      pos++;
      continue;
    }

    // Parse key
    std::string key = parseString(json, pos);
    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == ':') {
      pos++;
    }
    skipWhitespace(json, pos);

    // Parse value based on key
    if (key == "ph") {
      std::string phase = parseString(json, pos);
      if (!phase.empty()) {
        event.phase = phase[0];
      }
    } else if (key == "cat") {
      event.category = parseString(json, pos);
    } else if (key == "name") {
      event.name = parseString(json, pos);
    } else if (key == "ts") {
      event.timestamp_us = static_cast<uint64_t>(parseNumber(json, pos));
    } else if (key == "pid") {
      event.pid = static_cast<uint32_t>(parseNumber(json, pos));
    } else if (key == "tid") {
      event.tid = static_cast<uint32_t>(parseNumber(json, pos));
    } else if (key == "id") {
      // ID can be a string like "0x1" or a number
      if (json[pos] == '"') {
        std::string id_str = parseString(json, pos);
        try {
          if (id_str.size() > 2 && id_str.substr(0, 2) == "0x") {
            event.id = std::stoull(id_str.substr(2), nullptr, 16);
          } else {
            event.id = std::stoull(id_str);
          }
        } catch (...) {
          event.id = 0;
        }
      } else {
        event.id = static_cast<uint64_t>(parseNumber(json, pos));
      }
    } else if (key == "args") {
      // Parse args object
      skipWhitespace(json, pos);
      if (pos < json.size() && json[pos] == '{') {
        pos++;
        while (pos < json.size()) {
          skipWhitespace(json, pos);
          if (json[pos] == '}') {
            pos++;
            break;
          }
          if (json[pos] == ',') {
            pos++;
            continue;
          }

          std::string arg_key = parseString(json, pos);
          skipWhitespace(json, pos);
          if (pos < json.size() && json[pos] == ':') {
            pos++;
          }
          skipWhitespace(json, pos);

          // Determine value type
          if (json[pos] == '"') {
            event.string_args[arg_key] = parseString(json, pos);
          } else if (json[pos] == '-' || json[pos] == '+' ||
                     std::isdigit(json[pos])) {
            event.double_args[arg_key] = parseNumber(json, pos);
          } else {
            // Skip unknown value types (booleans, nulls, nested objects)
            int depth = 0;
            while (pos < json.size()) {
              if (json[pos] == '{' || json[pos] == '[')
                depth++;
              else if (json[pos] == '}' || json[pos] == ']') {
                if (depth == 0)
                  break;
                depth--;
              } else if (json[pos] == ',' && depth == 0)
                break;
              pos++;
            }
          }
        }
      }
    } else {
      // Skip unknown fields
      if (pos < json.size() && json[pos] == '"') {
        parseString(json, pos);
      } else if (pos < json.size() && (json[pos] == '{' || json[pos] == '[')) {
        int depth = 1;
        pos++;
        while (pos < json.size() && depth > 0) {
          if (json[pos] == '{' || json[pos] == '[')
            depth++;
          else if (json[pos] == '}' || json[pos] == ']')
            depth--;
          pos++;
        }
      } else if (pos < json.size() && (json[pos] == '-' || json[pos] == '+' ||
                                       std::isdigit(json[pos]))) {
        parseNumber(json, pos);
      } else {
        // Skip boolean/null
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}') {
          pos++;
        }
      }
    }
  }

  return event;
}

// Calculate percentile from sorted values
double percentile(const std::vector<double> &sorted, double p) {
  if (sorted.empty())
    return 0.0;
  size_t idx = static_cast<size_t>(sorted.size() * p / 100.0);
  if (idx >= sorted.size())
    idx = sorted.size() - 1;
  return sorted[idx];
}

} // namespace

std::vector<TraceEvent> loadTraceFile(const std::string &trace_file_path) {
  std::vector<TraceEvent> events;

  std::string json = readFile(trace_file_path);
  if (json.empty()) {
    return events;
  }

  // Find traceEvents array
  size_t pos = json.find("\"traceEvents\"");
  if (pos == std::string::npos) {
    return events;
  }

  // Skip to array start
  pos = json.find('[', pos);
  if (pos == std::string::npos) {
    return events;
  }
  pos++; // skip '['

  // Parse events
  while (pos < json.size()) {
    skipWhitespace(json, pos);
    if (pos >= json.size() || json[pos] == ']') {
      break;
    }
    if (json[pos] == ',') {
      pos++;
      continue;
    }

    TraceEvent event = parseTraceEvent(json, pos);
    if (!event.name.empty()) {
      events.push_back(event);
    }
  }

  return events;
}

std::vector<double> calculateDurations(const std::vector<TraceEvent> &events,
                                       const std::string &name) {
  std::vector<double> durations;

  // For scoped events (B/E): track by thread ID
  std::unordered_map<uint32_t, uint64_t> scoped_begin_times;

  // For async events (S/F): track by event ID
  std::unordered_map<uint64_t, uint64_t> async_begin_times;

  for (const auto &event : events) {
    if (event.name != name) {
      continue;
    }

    if (event.phase == 'B') {
      // Scoped begin - track by thread ID
      scoped_begin_times[event.tid] = event.timestamp_us;
    } else if (event.phase == 'E') {
      // Scoped end - match by thread ID
      auto it = scoped_begin_times.find(event.tid);
      if (it != scoped_begin_times.end()) {
        double duration_ms =
            static_cast<double>(event.timestamp_us - it->second) / 1000.0;
        durations.push_back(duration_ms);
        scoped_begin_times.erase(it);
      }
    } else if (event.phase == 'S') {
      // Async start - track by event ID
      async_begin_times[event.id] = event.timestamp_us;
    } else if (event.phase == 'F') {
      // Async finish - match by event ID
      auto it = async_begin_times.find(event.id);
      if (it != async_begin_times.end()) {
        double duration_ms =
            static_cast<double>(event.timestamp_us - it->second) / 1000.0;
        durations.push_back(duration_ms);
        async_begin_times.erase(it);
      }
    }
  }

  return durations;
}

BenchmarkStats calculateStats(const std::string &name,
                              const std::vector<double> &durations) {
  BenchmarkStats stats;
  stats.name = name;
  stats.count = durations.size();

  if (durations.empty()) {
    return stats;
  }

  std::vector<double> sorted = durations;
  std::sort(sorted.begin(), sorted.end());

  stats.min_ms = sorted.front();
  stats.max_ms = sorted.back();

  double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
  stats.avg_ms = sum / sorted.size();

  stats.p50_ms = percentile(sorted, 50);
  stats.p95_ms = percentile(sorted, 95);
  stats.p99_ms = percentile(sorted, 99);

  return stats;
}

BenchmarkStats analyzeTraceFile(const std::string &trace_file_path,
                                const std::string &event_name) {
  auto events = loadTraceFile(trace_file_path);
  auto durations = calculateDurations(events, event_name);
  return calculateStats(event_name, durations);
}

std::map<std::string, BenchmarkStats>
analyzeTraceFile(const std::string &trace_file_path,
                 const std::vector<std::string> &event_names) {
  std::map<std::string, BenchmarkStats> results;

  auto events = loadTraceFile(trace_file_path);

  for (const auto &name : event_names) {
    auto durations = calculateDurations(events, name);
    results[name] = calculateStats(name, durations);
  }

  return results;
}

void printStats(const BenchmarkStats &stats) {
  std::cout << "\n========================================" << std::endl;
  std::cout << "  " << stats.name << std::endl;
  std::cout << "========================================" << std::endl;

  if (stats.count == 0) {
    std::cout << "No measurements collected" << std::endl;
    return;
  }

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Samples:      " << stats.count << std::endl;
  std::cout << "Min:          " << stats.min_ms << " ms" << std::endl;
  std::cout << "Avg:          " << stats.avg_ms << " ms" << std::endl;
  std::cout << "P50:          " << stats.p50_ms << " ms" << std::endl;
  std::cout << "P95:          " << stats.p95_ms << " ms" << std::endl;
  std::cout << "P99:          " << stats.p99_ms << " ms" << std::endl;
  std::cout << "Max:          " << stats.max_ms << " ms" << std::endl;
  std::cout << "========================================\n" << std::endl;
}

void printComparisonTable(const std::vector<BenchmarkStats> &results) {
  if (results.empty()) {
    return;
  }

  // Find max name length for formatting
  size_t max_name_len = 10;
  for (const auto &stats : results) {
    max_name_len = std::max(max_name_len, stats.name.size());
  }

  // Print header
  std::cout << "\n"
            << std::left << std::setw(max_name_len + 2) << "Metric"
            << std::right << std::setw(8) << "Count" << std::setw(10) << "Min"
            << std::setw(10) << "Avg" << std::setw(10) << "P50" << std::setw(10)
            << "P95" << std::setw(10) << "P99" << std::setw(10) << "Max"
            << std::endl;

  std::cout << std::string(max_name_len + 2 + 68, '-') << std::endl;

  // Print rows
  std::cout << std::fixed << std::setprecision(1);
  for (const auto &stats : results) {
    std::cout << std::left << std::setw(max_name_len + 2) << stats.name
              << std::right << std::setw(8) << stats.count << std::setw(10)
              << stats.min_ms << std::setw(10) << stats.avg_ms << std::setw(10)
              << stats.p50_ms << std::setw(10) << stats.p95_ms << std::setw(10)
              << stats.p99_ms << std::setw(10) << stats.max_ms << std::endl;
  }
  std::cout << std::endl;
}

std::string exportToCsv(const std::vector<BenchmarkStats> &results) {
  std::ostringstream csv;

  // Header
  csv << "Metric,Count,Min_ms,Avg_ms,P50_ms,P95_ms,P99_ms,Max_ms\n";

  // Data rows
  csv << std::fixed << std::setprecision(3);
  for (const auto &stats : results) {
    csv << stats.name << "," << stats.count << "," << stats.min_ms << ","
        << stats.avg_ms << "," << stats.p50_ms << "," << stats.p95_ms << ","
        << stats.p99_ms << "," << stats.max_ms << "\n";
  }

  return csv.str();
}

} // namespace benchmark
} // namespace test
} // namespace livekit
