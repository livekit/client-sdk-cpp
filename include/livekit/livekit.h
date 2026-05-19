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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "livekit/logging.h"
#include "livekit/visibility.h"

namespace livekit {

/// The log sink to use for SDK messages.
/// @deprecated Use livekit::setLogCallback instead to register a custom log callback
enum class [[deprecated("Use livekit::setLogCallback instead to register a custom log callback")]] LogSink {
  /// Log messages to the console.
  kConsole = 0,
  /// Log messages to a callback function.
  kCallback = 1,
};

/// Initialize the LiveKit SDK.
///
/// This **must be the first LiveKit API called** in the process.
/// It configures global SDK state, including log routing.
///
/// @param level     Minimum log level for SDK messages (default: Info).
///                  Use setLogLevel() to change at runtime.
/// @param log_sink  The log sink to use for SDK messages (default: Console).
/// @returns true if initialization happened on this call, false if it was
///          already initialized.
/// @deprecated Use livekit::setLogCallback instead to register a custom log callback
[[deprecated("Use livekit::setLogCallback instead to register a custom log callback")]] LIVEKIT_API bool initialize(
    const LogLevel& level, const LogSink& log_sink);

/// Initialize the LiveKit SDK.
///
/// This **must be the first LiveKit API called** in the process.
/// It configures global SDK state.
///
/// @param level     Minimum log level for SDK messages (default: Info).
///                  Use setLogLevel() to change at runtime.
LIVEKIT_API bool initialize(const LogLevel& level = LogLevel::Info);

/// Shut down the LiveKit SDK.
///
/// After shutdown, you may call initialize() again.
LIVEKIT_API void shutdown();

} // namespace livekit