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

#ifndef LIVEKIT_LK_LOG_H
#define LIVEKIT_LK_LOG_H

#include <memory>
#include <spdlog/spdlog.h>

namespace livekit {
namespace detail {

/// Returns the shared "livekit" logger instance.
/// The logger is created lazily on first access and lives until
/// shutdownLogger() is called.  Safe to call before initialize().
std::shared_ptr<spdlog::logger> getLogger();

/// Tears down the spdlog logger. Called by livekit::shutdown().
void shutdownLogger();

} // namespace detail
} // namespace livekit

// Convenience macros — two-tier filtering:
//
//  1. Compile-time:  SPDLOG_ACTIVE_LEVEL (set via CMake LIVEKIT_LOG_LEVEL)
//     strips calls below the threshold entirely — zero overhead, no format
//     string evaluation, no function call.
//
//  2. Runtime:  livekit::setLogLevel() filters among the surviving levels.
//
// Default LIVEKIT_LOG_LEVEL is TRACE (nothing stripped).  For release builds
// consider -DLIVEKIT_LOG_LEVEL=INFO or WARN to eliminate verbose calls.
#define LK_LOG_TRACE(...)                                                      \
  SPDLOG_LOGGER_TRACE(livekit::detail::getLogger(), __VA_ARGS__)
#define LK_LOG_DEBUG(...)                                                      \
  SPDLOG_LOGGER_DEBUG(livekit::detail::getLogger(), __VA_ARGS__)
#define LK_LOG_INFO(...)                                                       \
  SPDLOG_LOGGER_INFO(livekit::detail::getLogger(), __VA_ARGS__)
#define LK_LOG_WARN(...)                                                       \
  SPDLOG_LOGGER_WARN(livekit::detail::getLogger(), __VA_ARGS__)
#define LK_LOG_ERROR(...)                                                      \
  SPDLOG_LOGGER_ERROR(livekit::detail::getLogger(), __VA_ARGS__)
#define LK_LOG_CRITICAL(...)                                                   \
  SPDLOG_LOGGER_CRITICAL(livekit::detail::getLogger(), __VA_ARGS__)

#endif /* LIVEKIT_LK_LOG_H */
