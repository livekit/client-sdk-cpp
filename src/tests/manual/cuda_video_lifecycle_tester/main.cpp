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
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {

using namespace std::chrono_literals;

constexpr int kDefaultIterations = 100;
constexpr int kVideoWidth = 640;
constexpr int kVideoHeight = 360;
constexpr int kFramesPerIteration = 500;
constexpr char kTrackName[] = "cuda-lifecycle-video";
constexpr auto kH264VideoCodec = static_cast<livekit::VideoCodec>(1);

struct Configuration {
  std::string url;
  std::string sender_token;
  std::string receiver_token;
};

struct Options {
  int iteration_count{kDefaultIterations};
};

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

Options parseOptions(int argc, char* argv[]) {
  Options options;
  if (argc == 1) {
    return options;
  }
  if (argc == 3 && std::strcmp(argv[1], "--iterations") == 0) {
    options.iteration_count = parseIterationCount(argv[2]);
    return options;
  }
  if (argc == 2) {
    options.iteration_count = parseIterationCount(argv[1]);
    return options;
  }
  throw std::runtime_error("usage: cuda_video_lifecycle_tester [--iterations N]");
}

std::shared_ptr<livekit::LocalParticipant> localParticipant(livekit::Room& room) {
  auto participant = room.localParticipant().lock();
  if (!participant) {
    throw std::runtime_error("local participant is unavailable");
  }
  return participant;
}

void runIteration(const Configuration& config) {
  livekit::Room receiver_room;
  livekit::Room sender_room;
  std::mutex mutex;
  std::condition_variable frame_received;
  bool received_frame = false;

  try {
    if (!receiver_room.connect(config.url, config.receiver_token, {})) {
      throw std::runtime_error("receiver failed to connect");
    }
    if (!sender_room.connect(config.url, config.sender_token, {})) {
      throw std::runtime_error("sender failed to connect");
    }

    const std::string sender_identity = localParticipant(sender_room)->identity();
    if (sender_identity.empty()) {
      throw std::runtime_error("sender identity is empty");
    }

    receiver_room.setOnVideoFrameCallback(
        sender_identity, kTrackName,
        [&mutex, &frame_received, &received_frame](const livekit::VideoFrame&, std::int64_t) {
          std::lock_guard<std::mutex> lock(mutex);
          received_frame = true;
          frame_received.notify_all();
        });

    auto source = std::make_shared<livekit::VideoSource>(kVideoWidth, kVideoHeight);
    auto track = livekit::LocalVideoTrack::createLocalVideoTrack(kTrackName, source);
    if (!track) {
      throw std::runtime_error("failed to create local video track");
    }

    livekit::TrackPublishOptions publish_options;
    publish_options.source = livekit::TrackSource::SOURCE_CAMERA;
    publish_options.simulcast = false;
    publish_options.video_codec = kH264VideoCodec;
    localParticipant(sender_room)->publishTrack(track, publish_options);

    auto frame = livekit::VideoFrame::create(kVideoWidth, kVideoHeight, livekit::VideoBufferType::I420);
    std::fill(frame.data(), frame.data() + frame.dataSize(), 0x7f);
    for (int frame_index = 0; frame_index < kFramesPerIteration; ++frame_index) {
      source->captureFrame(frame);

      std::unique_lock<std::mutex> lock(mutex);
      if (frame_received.wait_for(lock, 20ms, [&received_frame] { return received_frame; })) {
        break;
      }
    }

    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!received_frame) {
        throw std::runtime_error("timed out waiting for the receiver to get a video frame");
      }
    }

    if (const auto publication = track->publication()) {
      localParticipant(sender_room)->unpublishTrack(publication->sid());
    }
    receiver_room.clearOnVideoFrameCallback(sender_identity, kTrackName);
  } catch (...) {
    (void)sender_room.disconnect();
    (void)receiver_room.disconnect();
    throw;
  }

  if (!sender_room.disconnect()) {
    throw std::runtime_error("sender failed to disconnect");
  }
  if (!receiver_room.disconnect()) {
    throw std::runtime_error("receiver failed to disconnect");
  }
}

} // namespace

int main(int argc, char* argv[]) {
  try {
    const Options options = parseOptions(argc, argv);
    const Configuration config{
        requiredEnvironment("LIVEKIT_URL"),
        requiredEnvironment("LIVEKIT_TOKEN_A"),
        requiredEnvironment("LIVEKIT_TOKEN_B"),
    };
    const int progress_interval = options.iteration_count < 10 ? 1 : options.iteration_count / 10;

    std::cout << "Running " << options.iteration_count << " CUDA video lifecycle cycles\n";
    for (int iteration = 1; iteration <= options.iteration_count; ++iteration) {
      if (!livekit::initialize(livekit::LogLevel::Info)) {
        throw std::runtime_error("initialize failed at iteration " + std::to_string(iteration));
      }

      try {
        runIteration(config);
      } catch (...) {
        livekit::shutdown();
        throw;
      }
      livekit::shutdown();

      if (iteration % progress_interval == 0 || iteration == options.iteration_count) {
        std::cout << "Completed " << iteration << "/" << options.iteration_count << " cycles\n";
      }
    }
  } catch (const std::exception& error) {
    std::cerr << "CUDA video lifecycle tester failed: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
