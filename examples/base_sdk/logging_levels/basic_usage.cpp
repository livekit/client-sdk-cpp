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

/// @file main.cpp
/// @brief Demonstrates LiveKit SDK log-level control and custom log callbacks.
///
/// Logging has two tiers of filtering:
///
///   1. **Compile-time** (LIVEKIT_LOG_LEVEL, set via CMake):
///      Calls below this level are stripped from the binary entirely.
///      Default is TRACE (nothing stripped). For a lean release build:
///        cmake -DLIVEKIT_LOG_LEVEL=WARN ...
///
///   2. **Runtime** (setLogLevel()):
///      Among the levels that survived compilation, setLogLevel() controls
///      which ones actually produce output. This is what this example demos.
///
/// Usage:
///   LoggingLevels [trace|debug|info|warn|error|critical|off]
///
/// If no argument is given, the example cycles through every level so you can
/// see which messages are filtered at each setting.

#include "livekit/livekit.h"

#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

const char *levelName(livekit::LogLevel level) {
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
  return "UNKNOWN";
}

livekit::LogLevel parseLevel(const char *arg) {
  if (std::strcmp(arg, "trace") == 0)
    return livekit::LogLevel::Trace;
  if (std::strcmp(arg, "debug") == 0)
    return livekit::LogLevel::Debug;
  if (std::strcmp(arg, "info") == 0)
    return livekit::LogLevel::Info;
  if (std::strcmp(arg, "warn") == 0)
    return livekit::LogLevel::Warn;
  if (std::strcmp(arg, "error") == 0)
    return livekit::LogLevel::Error;
  if (std::strcmp(arg, "critical") == 0)
    return livekit::LogLevel::Critical;
  if (std::strcmp(arg, "off") == 0)
    return livekit::LogLevel::Off;
  std::cerr << "Unknown level '" << arg << "', defaulting to Info.\n"
            << "Valid: trace, debug, info, warn, error, critical, off\n";
  return livekit::LogLevel::Info;
}

/// Emit one message at every severity level using the LK_LOG_* macros.
void emitAllLevels() {
  LK_LOG_TRACE("This is a TRACE message (very verbose internals)");
  LK_LOG_DEBUG("This is a DEBUG message (diagnostic detail)");
  LK_LOG_INFO("This is an INFO message (normal operation)");
  LK_LOG_WARN("This is a WARN message (something unexpected)");
  LK_LOG_ERROR("This is an ERROR message (something failed)");
  LK_LOG_CRITICAL("This is a CRITICAL message (unrecoverable)");
}

/// Demonstrate cycling through every log level.
void runLevelCycleDemo() {
  const livekit::LogLevel levels[] = {
      livekit::LogLevel::Trace, livekit::LogLevel::Debug,
      livekit::LogLevel::Info,  livekit::LogLevel::Warn,
      livekit::LogLevel::Error, livekit::LogLevel::Critical,
      livekit::LogLevel::Off,
  };

  for (auto level : levels) {
    std::cout << "\n========================================\n"
              << " Setting log level to: " << levelName(level) << "\n"
              << "========================================\n";
    livekit::setLogLevel(level);
    emitAllLevels();
  }
}

/// Demonstrate a custom log callback (e.g. for ROS2 integration).
void runCallbackDemo() {
  std::cout << "\n========================================\n"
            << " Custom LogCallback demo\n"
            << "========================================\n";

  livekit::setLogLevel(livekit::LogLevel::Trace);

  // Install a user-defined callback that captures all log output.
  // In a real ROS2 node you would replace this with RCLCPP_* macros.
  livekit::setLogCallback([](livekit::LogLevel level,
                             const std::string &logger_name,
                             const std::string &message) {
    std::cout << "[CALLBACK] [" << levelName(level) << "] [" << logger_name
              << "] " << message << "\n";
  });

  LK_LOG_INFO("This message is routed through the custom callback");
  LK_LOG_WARN("Warnings also go through the callback");
  LK_LOG_ERROR("Errors too -- the callback sees everything >= the level");

  // Restore default stderr sink by passing an empty callback.
  livekit::setLogCallback(nullptr);

  std::cout << "\n(Restored default stderr sink)\n";
  LK_LOG_INFO("This message goes to stderr again (default sink)");
}

} // namespace

int main(int argc, char *argv[]) {
  // Initialize the LiveKit SDK (creates the spdlog logger).
  livekit::initialize();

  if (argc > 1) {
    // Single-level mode: set the requested level and emit all messages.
    livekit::LogLevel level = parseLevel(argv[1]);
    std::cout << "Setting log level to: " << levelName(level) << "\n\n";
    livekit::setLogLevel(level);
    emitAllLevels();
  } else {
    // Full demo: cycle through levels, then show the callback API.
    runLevelCycleDemo();
    runCallbackDemo();
  }

  livekit::shutdown();
  return 0;
}
