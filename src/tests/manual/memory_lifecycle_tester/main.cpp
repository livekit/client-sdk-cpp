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

#include <livekit/livekit.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kDefaultIterations = 1'000;
constexpr int kAudioSampleRate = 48'000;
constexpr int kAudioChannels = 1;
constexpr int kAudioQueueSizeMs = 100;
constexpr int kVideoWidth = 1'280;
constexpr int kVideoHeight = 720;
constexpr std::size_t kDataPayloadSize = 1'024;

int parseIterationCount(const char* value) {
  try {
    const int parsed = std::stoi(value);
    if (parsed <= 0) {
      throw std::runtime_error("iteration count must be greater than zero");
    }
    return parsed;
  } catch (const std::invalid_argument&) {
    throw std::runtime_error("iteration count must be an integer");
  } catch (const std::out_of_range&) {
    throw std::runtime_error("iteration count is out of range");
  }
}

void exerciseCommonFeatures() {
  auto audio_source = std::make_shared<livekit::AudioSource>(kAudioSampleRate, kAudioChannels, kAudioQueueSizeMs);
  auto audio_track = livekit::LocalAudioTrack::createLocalAudioTrack("lifecycle-audio", audio_source);
  if (!audio_track) {
    throw std::runtime_error("failed to create local audio track");
  }
  // Deliberately do not capture a frame. This covers teardown of the Rust
  // keepalive task used before a raw video source receives its first frame.
  auto video_source = std::make_shared<livekit::VideoSource>(kVideoWidth, kVideoHeight);
  auto video_track = livekit::LocalVideoTrack::createLocalVideoTrack("lifecycle-video", video_source);
  if (!video_track) {
    throw std::runtime_error("failed to create local video track");
  }

  // A LocalDataTrack requires a connected LocalParticipant. Constructing the
  // public frame type still covers the common offline data allocation surface.
  livekit::DataTrackFrame data_frame(std::vector<std::uint8_t>(kDataPayloadSize, 0x5a));
  if (data_frame.payload.size() != kDataPayloadSize) {
    throw std::runtime_error("failed to create data track frame");
  }
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc > 2) {
    std::cerr << "usage: " << argv[0] << " [iteration-count]\n";
    return 2;
  }

  try {
    const int iteration_count = argc == 2 ? parseIterationCount(argv[1]) : kDefaultIterations;
    std::cout << "Running " << iteration_count << " LiveKit initialize/shutdown cycles\n";

    for (int iteration = 1; iteration <= iteration_count; ++iteration) {
      if (!livekit::initialize(livekit::LogLevel::Warn)) {
        throw std::runtime_error("initialize failed at iteration " + std::to_string(iteration));
      }

      try {
        exerciseCommonFeatures();
      } catch (...) {
        livekit::shutdown();
        throw;
      }
      livekit::shutdown();

      if (iteration % 100 == 0 || iteration == iteration_count) {
        std::cout << "Completed " << iteration << "/" << iteration_count << " cycles\n";
      }
    }
  } catch (const std::exception& error) {
    std::cerr << "memory lifecycle tester failed: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
