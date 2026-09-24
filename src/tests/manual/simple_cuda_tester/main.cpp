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

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

constexpr int kDefaultIterations = 1;
constexpr int kVideoWidth = 640;
constexpr int kVideoHeight = 360;
constexpr int kTrackCount = 3;
constexpr int kFramesPerTrack = 30;
constexpr auto kH264VideoCodec = static_cast<livekit::VideoCodec>(1);
constexpr const char* kTrackNames[] = {"cuda-cam-0", "cuda-cam-1", "cuda-cam-2"};
constexpr const char* kUsage = "usage: livekit_simple_cuda_tester [--iterations] N";

const char* requiredEnvironment(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    throw std::runtime_error(std::string(name) + " must be set");
  }
  return value;
}

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

int parseOptions(int argc, char* argv[]) {
  if (argc == 1) {
    return kDefaultIterations;
  }
  if (argc == 2) {
    return parseIterationCount(argv[1]);
  }
  if (argc == 3 && std::strcmp(argv[1], "--iterations") == 0) {
    return parseIterationCount(argv[2]);
  }
  throw std::runtime_error(kUsage);
}

void preferNvencIfUnset() {
  const char* existing = std::getenv("LIVEKIT_PREFERRED_HW_ENCODER");
  if (existing != nullptr && existing[0] != '\0') {
    return;
  }
#if defined(_WIN32)
  _putenv_s("LIVEKIT_PREFERRED_HW_ENCODER", "nvenc");
#else
  setenv("LIVEKIT_PREFERRED_HW_ENCODER", "nvenc", 0);
#endif
}

std::shared_ptr<livekit::LocalParticipant> localParticipant(livekit::Room& room) {
  auto participant = room.localParticipant().lock();
  if (!participant) {
    throw std::runtime_error("local participant is unavailable");
  }
  return participant;
}

struct PublishedTrack {
  std::shared_ptr<livekit::VideoSource> source;
  std::shared_ptr<livekit::LocalVideoTrack> track;
};

PublishedTrack publishH264Track(livekit::LocalParticipant& participant, const char* name) {
  PublishedTrack published;
  published.source = std::make_shared<livekit::VideoSource>(kVideoWidth, kVideoHeight);
  published.track = livekit::LocalVideoTrack::createLocalVideoTrack(name, published.source);
  if (!published.track) {
    throw std::runtime_error(std::string("failed to create track ") + name);
  }

  livekit::TrackPublishOptions options;
  options.source = livekit::TrackSource::SOURCE_CAMERA;
  options.simulcast = false;
  options.video_codec = kH264VideoCodec;
  participant.publishTrack(published.track, options);
  return published;
}

void captureGrayFrames(const std::vector<PublishedTrack>& tracks) {
  auto frame = livekit::VideoFrame::create(kVideoWidth, kVideoHeight, livekit::VideoBufferType::I420);
  std::fill(frame.data(), frame.data() + frame.dataSize(), 0x7f);

  for (int frame_index = 0; frame_index < kFramesPerTrack; ++frame_index) {
    for (const auto& published : tracks) {
      published.source->captureFrame(frame);
    }
    std::this_thread::sleep_for(33ms);
  }
}

void unpublishTracks(livekit::LocalParticipant& participant, const std::vector<PublishedTrack>& tracks) {
  for (const auto& published : tracks) {
    if (const auto publication = published.track->publication()) {
      participant.unpublishTrack(publication->sid());
    }
  }
}

void run(const std::string& url, const std::string& token) {
  livekit::Room room;
  std::vector<PublishedTrack> tracks;
  tracks.reserve(kTrackCount);

  try {
    if (!room.connect(url, token, {})) {
      throw std::runtime_error("failed to connect to room");
    }
    std::cout << "Connected to room: " << room.roomInfo().name << '\n';

    auto participant = localParticipant(room);
    // for (int index = 0; index < kTrackCount; ++index) {
    //   tracks.push_back(publishH264Track(*participant, kTrackNames[index]));
    //   std::cout << "Published " << kTrackNames[index] << '\n';
    // }

    // captureGrayFrames(tracks);
    // std::cout << "Captured " << kFramesPerTrack << " frames on " << kTrackCount << " tracks\n";

    // unpublishTracks(*participant, tracks);
  } catch (...) {
    (void)room.disconnect();
    throw;
  }

  if (!room.disconnect()) {
    throw std::runtime_error("failed to disconnect");
  }
  std::cout << "Disconnected\n";
}

} // namespace

int main(int argc, char* argv[]) {
  try {
    const int iteration_count = parseOptions(argc, argv);
    preferNvencIfUnset();
    const std::string url = requiredEnvironment("LIVEKIT_URL");
    const std::string token = requiredEnvironment("LIVEKIT_TOKEN_A");

    if (!livekit::initialize(livekit::LogLevel::Info)) {
      throw std::runtime_error("initialize failed");
    }

    try {
      std::cout << "Running " << iteration_count << " CUDA publish cycles\n";
      for (int iteration = 1; iteration <= iteration_count; ++iteration) {
        std::cout << "----- cycle " << iteration << '/' << iteration_count << " -----\n";
        run(url, token);
      }
    } catch (...) {
      livekit::shutdown();
      throw;
    }
    livekit::shutdown();
  } catch (const std::exception& error) {
    std::cerr << "simple_cuda_tester failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
