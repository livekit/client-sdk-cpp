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

#pragma once

#include <functional>
#include <string>

namespace livekit {

/// Severity levels for SDK log messages.
enum class LogLevel {
  Trace = 0,
  Debug = 1,
  Info = 2,
  Warn = 3,
  Error = 4,
  Critical = 5,
  Off = 6,
};

/// Set the minimum log level for the SDK logger.
///
/// Messages below this level are discarded before reaching any sink
/// or callback. Thread-safe; may be called at any time after initialize().
void setLogLevel(LogLevel level);

/// Return the current minimum log level.
LogLevel getLogLevel();

/// Signature for a user-supplied log callback.
///
/// @param level     Severity of the message.
/// @param logger_name  Name of the originating logger (e.g. "livekit").
/// @param message   Formatted log message (no trailing newline).
///
/// The callback is invoked sequentially (never concurrently) from the
/// thread that generated the log message. Implementations must not block
/// for extended periods.
using LogCallback = std::function<void(LogLevel level, const std::string& logger_name, const std::string& message)>;

/// Install a custom log callback, replacing the default stderr sink.
///
/// Pass nullptr / empty function to restore the default stderr sink.
/// Thread-safe; may be called at any time after initialize().
void setLogCallback(LogCallback callback);

} // namespace livekit
