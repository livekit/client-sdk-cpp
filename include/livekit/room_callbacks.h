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

/// @brief Callback for incoming audio frames on a subscription reader thread.
using AudioFrameCallback = std::function<void(const AudioFrame&)>;

/// @brief Callback for incoming video frames on a subscription reader thread.
using VideoFrameCallback = std::function<void(const VideoFrame& frame, std::int64_t timestamp_us)>;

/// @brief Callback for incoming video frame events on a subscription reader thread.
using VideoFrameEventCallback = std::function<void(const VideoFrameEvent&)>;

/// @brief Callback for incoming data frames on a subscription reader thread.
using DataFrameCallback =
    std::function<void(const std::vector<std::uint8_t>& payload, std::optional<std::uint64_t> user_timestamp)>;

/// @brief Identifier for a data frame callback registration.
using DataFrameCallbackId = std::uint64_t;

} // namespace livekit
