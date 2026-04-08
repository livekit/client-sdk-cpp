/*
 * Copyright 2023 LiveKit
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

/// @file custom_sinks.cpp
/// @brief Shows how SDK consumers supply their own log backend via
///        livekit::setLogCallback().
///
/// This example uses ONLY the public SDK API (<livekit/livekit.h>).
/// No internal headers or spdlog dependency required.
///
/// Three patterns are demonstrated:
///
///   1. **File logger**    -- write SDK logs to a file on disk.
///   2. **JSON logger**    -- emit structured JSON lines (for log aggregation).
///   3. **ROS2 bridge**    -- skeleton showing how to route SDK logs into
///                            RCLCPP_* macros (the rclcpp headers are stubbed
///                            so this compiles without a ROS2 install).
///
/// Usage:
///   CustomSinks [file|json|ros2]
///
/// If no argument is given, all three sinks are demonstrated in sequence.

#include "livekit/livekit.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

// ---------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------

const char *levelTag(livekit::LogLevel level) {
  switch (level) {
  case livekit::LogLevel::Trace:
    return "TRACE";
  case livekit::LogLevel::Debug:
    return "DEBUG";
  case livekit::LogLevel::Info:
    return "INFO";
  case livekit::LogLevel::Warn:
    return "WARN";
  case livekit::LogLevel::Error:
    return "ERROR";
  case livekit::LogLevel::Critical:
    return "CRITICAL";
  case livekit::LogLevel::Off:
    return "OFF";
  }
  return "?";
}

std::string nowISO8601() {
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;
  std::ostringstream ss;
  ss << std::put_time(std::gmtime(&tt), "%FT%T") << '.' << std::setfill('0')
     << std::setw(3) << ms.count() << 'Z';
  return ss.str();
}

struct SampleLog {
  livekit::LogLevel level;
  const char *message;
};

// Representative messages that the SDK would emit during normal operation.
// We drive the installed callback directly so this example has zero internal
// dependencies -- only the public <livekit/logging.h> API.
const SampleLog kSampleLogs[] = {
    {livekit::LogLevel::Trace, "per-frame data: pts=12345 bytes=921600"},
    {livekit::LogLevel::Debug, "negotiating codec: VP8 -> H264 fallback"},
    {livekit::LogLevel::Info, "track published: sid=TR_abc123 kind=video"},
    {livekit::LogLevel::Warn, "ICE candidate pair changed unexpectedly"},
    {livekit::LogLevel::Error, "DTLS handshake failed: timeout after 10s"},
    {livekit::LogLevel::Critical, "out of memory allocating decode buffer"},
};

void driveCallback(const livekit::LogCallback &cb) {
  for (const auto &entry : kSampleLogs) {
    cb(entry.level, "livekit", entry.message);
  }
}

// ---------------------------------------------------------------
// 1. File logger
// ---------------------------------------------------------------

void runFileSinkDemo() {
  const char *path = "livekit.log";
  std::cout << "\n=== File sink: writing SDK logs to '" << path << "' ===\n";

  auto file = std::make_shared<std::ofstream>(path, std::ios::trunc);
  if (!file->is_open()) {
    std::cerr << "Could not open " << path << " for writing\n";
    return;
  }

  // The shared_ptr keeps the stream alive inside the lambda even if
  // the local variable goes out of scope before the callback fires.
  livekit::LogCallback fileSink = [file](livekit::LogLevel level,
                                         const std::string &logger_name,
                                         const std::string &message) {
    *file << nowISO8601() << " [" << levelTag(level) << "] [" << logger_name
          << "] " << message << "\n";
    file->flush();
  };

  // In a real app you would call:
  //   livekit::setLogCallback(fileSink);
  // and then SDK operations (room.connect, publishTrack, ...) would route
  // their internal log output through your callback automatically.
  //
  // Here we drive the callback directly with sample data so the example
  // is self-contained and doesn't require a LiveKit server.
  livekit::setLogCallback(fileSink);
  driveCallback(fileSink);
  livekit::setLogCallback(nullptr);

  std::cout << "Wrote " << path << " -- contents:\n\n";
  std::ifstream in(path);
  std::cout << in.rdbuf() << "\n";
}

// ---------------------------------------------------------------
// 2. JSON structured logger
// ---------------------------------------------------------------

std::string escapeJson(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += c;
    }
  }
  return out;
}

void runJsonSinkDemo() {
  std::cout << "\n=== JSON sink: structured log lines to stdout ===\n\n";

  livekit::LogCallback jsonSink = [](livekit::LogLevel level,
                                     const std::string &logger_name,
                                     const std::string &message) {
    std::cout << R"({"ts":")" << nowISO8601() << R"(","level":")"
              << levelTag(level) << R"(","logger":")" << escapeJson(logger_name)
              << R"(","msg":")" << escapeJson(message) << "\"}\n";
  };

  livekit::setLogCallback(jsonSink);
  driveCallback(jsonSink);
  livekit::setLogCallback(nullptr);
}

// ---------------------------------------------------------------
// 3. ROS2 bridge (stubbed -- compiles without rclcpp)
// ---------------------------------------------------------------
//
// In a real ROS2 node the lambda body would be:
//
//   switch (level) {
//   case livekit::LogLevel::Trace:
//   case livekit::LogLevel::Debug:
//     RCLCPP_DEBUG(node_->get_logger(), "[%s] %s",
//                  logger_name.c_str(), message.c_str());
//     break;
//   case livekit::LogLevel::Info:
//     RCLCPP_INFO(node_->get_logger(), "[%s] %s",
//                 logger_name.c_str(), message.c_str());
//     break;
//   case livekit::LogLevel::Warn:
//     RCLCPP_WARN(node_->get_logger(), "[%s] %s",
//                 logger_name.c_str(), message.c_str());
//     break;
//   case livekit::LogLevel::Error:
//   case livekit::LogLevel::Critical:
//     RCLCPP_ERROR(node_->get_logger(), "[%s] %s",
//                  logger_name.c_str(), message.c_str());
//     break;
//   default:
//     break;
//   }
//
// Here we stub it with console output that mimics ROS2 formatting.

void runRos2SinkDemo() {
  std::cout << "\n=== ROS2 bridge sink (stubbed) ===\n\n";

  const std::string node_name = "livekit_bridge_node";

  livekit::LogCallback ros2Sink = [&node_name](livekit::LogLevel level,
                                               const std::string &logger_name,
                                               const std::string &message) {
    const char *ros_level;
    switch (level) {
    case livekit::LogLevel::Trace:
    case livekit::LogLevel::Debug:
      ros_level = "DEBUG";
      break;
    case livekit::LogLevel::Info:
      ros_level = "INFO";
      break;
    case livekit::LogLevel::Warn:
      ros_level = "WARN";
      break;
    case livekit::LogLevel::Error:
    case livekit::LogLevel::Critical:
      ros_level = "ERROR";
      break;
    default:
      ros_level = "INFO";
      break;
    }

    // Mimic: [INFO] [1719500000.123] [livekit_bridge_node]: [livekit] msg
    auto epoch_s = std::chrono::duration<double>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
    std::cout << "[" << ros_level << "] [" << std::fixed << std::setprecision(3)
              << epoch_s << "] [" << node_name << "]: [" << logger_name << "] "
              << message << "\n";
  };

  livekit::setLogCallback(ros2Sink);
  driveCallback(ros2Sink);
  livekit::setLogCallback(nullptr);
}

} // namespace

int main(int argc, char *argv[]) {
  livekit::initialize();

  if (argc > 1) {
    if (std::strcmp(argv[1], "file") == 0) {
      runFileSinkDemo();
    } else if (std::strcmp(argv[1], "json") == 0) {
      runJsonSinkDemo();
    } else if (std::strcmp(argv[1], "ros2") == 0) {
      runRos2SinkDemo();
    } else {
      std::cerr << "Unknown sink '" << argv[1] << "'.\n"
                << "Usage: CustomSinks [file|json|ros2]\n";
    }
  } else {
    runFileSinkDemo();
    runJsonSinkDemo();
    runRos2SinkDemo();
  }

  livekit::shutdown();
  return 0;
}
