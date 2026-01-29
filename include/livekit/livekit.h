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
#include "audio_source.h"
#include "audio_stream.h"
#include "build.h"
#include "e2ee.h"
#include "local_audio_track.h"
#include "local_participant.h"
#include "local_track_publication.h"
#include "local_video_track.h"
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

/// Where LiveKit logs should go.
enum class LogSink {
  /// Logs are printed to the default console output (FFI prints directly).
  kConsole = 0,

  /// Logs are delivered to the application's FFI callback for capturing.
  kCallback = 1,
};

/// Initialize the LiveKit SDK.
///
/// This **must be the first LiveKit API called** in the process.
/// It configures global SDK state, including log routing.
///
/// If LiveKit APIs are used before calling this function, the log
/// configuration may not take effect as expected.
/// Returns true if initialization happened on this call, false if it was
/// already initialized.
bool initialize(LogSink log_sink = LogSink::kConsole);

/// Shut down the LiveKit SDK.
///
/// After shutdown, you may call initialize() again.
void shutdown();

} // namespace livekit