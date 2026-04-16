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

#ifndef LIVEKIT_TRACE_EVENT_TRACER_INTERNAL_H_
#define LIVEKIT_TRACE_EVENT_TRACER_INTERNAL_H_

#include <cstdint>
#include <string>
#include <vector>

namespace livekit {
namespace trace {
namespace internal {

/**
 * Start tracing and write events to the specified file.
 *
 * Opens the file, starts a background writer thread, and enables tracing.
 * Events are queued and written asynchronously to minimize latency impact.
 *
 * @param file_path Path to the output trace file
 * @param categories Categories to enable (empty = all)
 * @return true if tracing started successfully, false if already running or
 * file error
 */
bool StartTracing(const std::string &file_path,
                  const std::vector<std::string> &categories);

/**
 * Stop tracing and flush all pending events.
 *
 * Disables tracing, waits for the background thread to write all queued events,
 * finalizes the JSON output, and closes the file.
 */
void StopTracing();

/**
 * Check if tracing is currently enabled.
 *
 * @return true if tracing is active
 */
bool IsTracingEnabled();

} // namespace internal
} // namespace trace
} // namespace livekit

#endif // LIVEKIT_TRACE_EVENT_TRACER_INTERNAL_H_
