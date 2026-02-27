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

#include "audio_frame.h"
#include "audio_processing_module.h"
#include "audio_source.h"
#include "audio_stream.h"
#include "build.h"
#include "e2ee.h"
#include "local_audio_track.h"
#include "local_participant.h"
#include "local_track_publication.h"
#include "local_video_track.h"
#include "logging.h"
#include "participant.h"
#include "remote_participant.h"
#include "remote_track_publication.h"
#include "room.h"
#include "room_delegate.h"
#include "room_event_types.h"
#include "track_publication.h"
#include "video_frame.h"
#include "video_source.h"
#include "video_stream.h"

namespace livekit {

/// @deprecated Use setLogLevel() and setLogCallback() from logging.h instead.
enum class [[deprecated(
    "Use setLogLevel() and setLogCallback() instead")]] LogSink {
  kConsole = 0,
  kCallback = 1,
};

/// Initialize the LiveKit SDK.
///
/// This **must be the first LiveKit API called** in the process.
/// It configures global SDK state, including log routing.
///
/// @param level  Minimum log level for SDK messages (default: Info).
///               Use setLogLevel() to change at runtime.
///
/// Returns true if initialization happened on this call, false if it was
/// already initialized.
bool initialize(LogLevel level = LogLevel::Info);

/// @deprecated Use initialize(LogLevel) instead.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((deprecated("Use initialize(LogLevel) instead")))
#endif
bool initialize(LogSink log_sink);

/// Shut down the LiveKit SDK.
///
/// After shutdown, you may call initialize() again.
void shutdown();

} // namespace livekit