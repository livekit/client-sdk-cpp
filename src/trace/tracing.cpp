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

#include "livekit/tracing.h"

#include "trace/event_tracer_internal.h"

namespace livekit {

bool startTracing(const std::string& trace_file_path, const std::vector<std::string>& categories) {
  return trace::internal::StartTracing(trace_file_path, categories);
}

void stopTracing() { trace::internal::StopTracing(); }

bool isTracingEnabled() { return trace::internal::IsTracingEnabled(); }

} // namespace livekit
