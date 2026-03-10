/*
 * Copyright 2025 LiveKit, Inc.
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

#include <chrono>
#include <memory>

// Assuming you already have this somewhere:
extern std::atomic<bool> g_running;

namespace livekit {
class AudioSource;
class VideoSource;
} // namespace livekit

void runNoiseCaptureLoop(const std::shared_ptr<livekit::AudioSource> &source,
                         std::atomic<bool> &running_flag);

void runFakeVideoCaptureLoop(
    const std::shared_ptr<livekit::VideoSource> &source,
    std::atomic<bool> &running_flag);
