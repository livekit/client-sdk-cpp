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

#include <string>
#include <vector>

namespace livekit {

/**
 * Start tracing and write events to a file.
 *
 * Events are written to the file asynchronously by a background thread.
 * The file is written in Chrome trace format (JSON), viewable in:
 *   - Chrome: chrome://tracing
 *   - Perfetto: https://ui.perfetto.dev
 *
 * @param trace_file_path Path to the output trace file (e.g., "trace.json")
 * @param categories Categories to enable (empty = all categories).
 *                   Supports wildcards: "livekit.*" matches all livekit
 * categories.
 * @return true if tracing was started, false if already running or file error
 */
bool startTracing(const std::string &trace_file_path,
                  const std::vector<std::string> &categories = {});

/**
 * Stop tracing and flush remaining events to file.
 *
 * This blocks until all pending events are written and the file is closed.
 * After stopping, the trace file is complete and ready for analysis.
 */
void stopTracing();

/**
 * Check if tracing is currently active.
 *
 * @return true if tracing is running
 */
bool isTracingEnabled();

} // namespace livekit
