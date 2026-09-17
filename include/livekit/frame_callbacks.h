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

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace livekit {

class AudioFrame;
class VideoFrame;
struct VideoFrameEvent;

/// Callback type for incoming audio frames.
/// Invoked on a dedicated reader thread per (participant, track_name) pair.
using AudioFrameCallback = std::function<void(const AudioFrame&)>;

/// Callback type for incoming video frames.
/// Invoked on a dedicated reader thread per (participant, track_name) pair.
using VideoFrameCallback = std::function<void(const VideoFrame& frame, std::int64_t timestamp_us)>;

/// Callback type for incoming video frame events.
/// Invoked on a dedicated reader thread per (participant, track_name) pair.
using VideoFrameEventCallback = std::function<void(const VideoFrameEvent&)>;

/// Callback type for incoming data track frames.
/// Invoked on a dedicated reader thread per subscription.
/// @param payload        Raw binary data received.
/// @param user_timestamp Optional application-defined timestamp from sender.
using DataFrameCallback =
    std::function<void(const std::vector<std::uint8_t>& payload, std::optional<std::uint64_t> user_timestamp)>;

/// Opaque identifier returned by addOnDataFrameCallback, used to remove an
/// individual subscription via removeOnDataFrameCallback.
using DataFrameCallbackId = std::uint64_t;

} // namespace livekit
