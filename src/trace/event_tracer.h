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
 *
 * Based on WebRTC's event_tracer.h:
 * Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 * Use of this source code is governed by a BSD-style license.
 */

// NOLINTBEGIN
// External code: this header is derived from WebRTC's event_tracer.h

// This file defines the interface for event tracing in LiveKit SDK.
//
// Event log handlers are set through SetupEventTracer(). User of this API will
// provide two function pointers to handle event tracing calls.
//
// * GetCategoryEnabledPtr
//   Event tracing system calls this function to determine if a particular
//   event category is enabled.
//
// * AddTraceEventPtr
//   Adds a tracing event. It is the user's responsibility to log the data
//   provided.
//
// Parameters for the above two functions are described in trace_event.h.

#ifndef LIVEKIT_TRACE_EVENT_TRACER_H_
#define LIVEKIT_TRACE_EVENT_TRACER_H_

// Platform-specific DLL export macro
#if defined(_WIN32)
#if defined(LIVEKIT_BUILDING_SDK)
#define LIVEKIT_EXPORT __declspec(dllexport)
#else
#define LIVEKIT_EXPORT __declspec(dllimport)
#endif
#else
#define LIVEKIT_EXPORT __attribute__((visibility("default")))
#endif

namespace livekit {
namespace trace {

typedef const unsigned char *(*GetCategoryEnabledPtr)(const char *name);
typedef void (*AddTraceEventPtr)(char phase,
                                 const unsigned char *category_enabled,
                                 const char *name, unsigned long long id,
                                 int num_args, const char **arg_names,
                                 const unsigned char *arg_types,
                                 const unsigned long long *arg_values,
                                 unsigned char flags);

// User of LiveKit SDK can call this method to setup custom event tracing.
//
// This method must be called before any tracing begins. Functions
// provided should be thread-safe.
LIVEKIT_EXPORT void
SetupEventTracer(GetCategoryEnabledPtr get_category_enabled_ptr,
                 AddTraceEventPtr add_trace_event_ptr);

// This class defines interface for the event tracing system to call
// internally. Do not call these methods directly.
class EventTracer {
public:
  static const unsigned char *GetCategoryEnabled(const char *name);

  static void AddTraceEvent(char phase, const unsigned char *category_enabled,
                            const char *name, unsigned long long id,
                            int num_args, const char **arg_names,
                            const unsigned char *arg_types,
                            const unsigned long long *arg_values,
                            unsigned char flags);
};

} // namespace trace
} // namespace livekit

// Compatibility aliases for trace_event.h macros (which use webrtc:: namespace)
namespace webrtc {
using namespace livekit::trace;
} // namespace webrtc

// NOLINTEND

#endif // LIVEKIT_TRACE_EVENT_TRACER_H_
