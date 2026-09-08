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
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
constexpr int kAudioFrameDurationMs = 10;
constexpr int kVideoWidth = 1'280;
constexpr int kVideoHeight = 720;
constexpr int kMediaFrameCount = 3;
constexpr std::size_t kDataPayloadSize = 1'024;
constexpr int kDataFrameCount = 10;
constexpr char kDataTrackName[] = "lifecycle-data";

struct Configuration {
  std::string url;
  std::string token;
};

struct Options {
  int iteration_count{kDefaultIterations};
  bool sources{false};
  bool connect{false};
  bool media{false};
  bool data_track{false};
  bool data_frames{false};
  bool ffi_cycles{false};
  bool mode_selected{false};
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

void runUnusedSources() {
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
}

template <typename LocalTrackType>
void unpublishTrackIfPublished(const std::shared_ptr<livekit::LocalParticipant>& participant,
                               const std::shared_ptr<LocalTrackType>& track) {
  const auto publication = track->publication();
  if (publication) {
    participant->unpublishTrack(publication->sid());
  }
}

void runMediaWork(const std::shared_ptr<livekit::LocalParticipant>& participant) {
  auto audio_source = std::make_shared<livekit::AudioSource>(kAudioSampleRate, kAudioChannels, 0);
  auto audio_track = livekit::LocalAudioTrack::createLocalAudioTrack("lifecycle-audio-active", audio_source);
  if (!audio_track) {
    throw std::runtime_error("failed to create active local audio track");
  }

  auto video_source = std::make_shared<livekit::VideoSource>(kVideoWidth, kVideoHeight);
  auto video_track = livekit::LocalVideoTrack::createLocalVideoTrack("lifecycle-video-active", video_source);
  if (!video_track) {
    throw std::runtime_error("failed to create active local video track");
  }

  livekit::TrackPublishOptions audio_options;
  audio_options.source = livekit::TrackSource::SOURCE_MICROPHONE;
  livekit::TrackPublishOptions video_options;
  video_options.source = livekit::TrackSource::SOURCE_CAMERA;
  video_options.simulcast = false;

  participant->publishTrack(audio_track, audio_options);
  participant->publishTrack(video_track, video_options);

  auto audio_frame =
      livekit::AudioFrame::create(kAudioSampleRate, kAudioChannels, kAudioSampleRate * kAudioFrameDurationMs / 1'000);
  auto video_frame = livekit::VideoFrame::create(kVideoWidth, kVideoHeight, livekit::VideoBufferType::RGBA);
  std::fill(video_frame.data(), video_frame.data() + video_frame.dataSize(), 0x40);

  for (int frame = 0; frame < kMediaFrameCount; ++frame) {
    audio_source->captureFrame(audio_frame, 1'000);
    video_source->captureFrame(video_frame);
  }

  unpublishTrackIfPublished(participant, video_track);
  unpublishTrackIfPublished(participant, audio_track);
}

void runRoomIteration(const Configuration& config, const Options& options) {
  livekit::Room room;
  if (!room.connect(config.url, config.token, {})) {
    throw std::runtime_error("failed to connect to the LiveKit room");
  }

  try {
    auto participant = room.localParticipant().lock();
    if (!participant) {
      throw std::runtime_error("local participant is unavailable");
    }

    if (options.media) {
      runMediaWork(participant);
    }

    if (options.data_track) {
      auto result = participant->publishDataTrack(kDataTrackName);
      if (!result) {
        throw std::runtime_error("failed to publish data track: " + result.error().message);
      }
      const auto& data_track = result.value();

      if (options.data_frames) {
        const livekit::DataTrackFrame data_frame(std::vector<std::uint8_t>(kDataPayloadSize, 0x5a));
        for (int frame = 0; frame < kDataFrameCount; ++frame) {
          auto push_result = data_track->tryPush(data_frame);
          if (!push_result) {
            throw std::runtime_error("failed to publish data: " + push_result.error().message);
          }
        }
      }

      data_track->unpublishDataTrack();
    }
  } catch (...) {
    (void)room.disconnect();
    throw;
  }

  if (!room.disconnect()) {
    throw std::runtime_error("failed to disconnect from the LiveKit room");
  }
}

void printUsage(const char* executable) {
  std::cerr << "usage: " << executable
            << " [--iterations N] [--ffi-cycles] [--sources] [--connect] [--media] [--data-track] [--data-frames]\n"
            << "  --ffi-cycles   Initialize and shut down the SDK on every iteration.\n"
            << "  --sources      Create and drop unused local audio/video sources and tracks.\n"
            << "  --connect      Connect to and leave a room.\n"
            << "  --media        Publish, capture, and unpublish audio/video tracks (implies --connect).\n"
            << "  --data-track   Publish and unpublish a data track (implies --connect).\n"
            << "  --data-frames  Send data frames (implies --data-track and --connect).\n"
            << "  By default the SDK is initialized once and all workloads run on every iteration.\n"
            << "  A single numeric argument remains supported as the iteration count.\n";
}

Options parseOptions(int argc, char* argv[]) {
  Options options;
  for (int argument = 1; argument < argc; ++argument) {
    const char* value = argv[argument];
    if (std::strcmp(value, "--iterations") == 0) {
      if (++argument == argc) {
        throw std::runtime_error("--iterations requires a value");
      }
      options.iteration_count = parseIterationCount(argv[argument]);
    } else if (std::strcmp(value, "--sources") == 0) {
      options.sources = true;
      options.mode_selected = true;
    } else if (std::strcmp(value, "--connect") == 0) {
      options.connect = true;
      options.mode_selected = true;
    } else if (std::strcmp(value, "--media") == 0) {
      options.media = true;
      options.mode_selected = true;
    } else if (std::strcmp(value, "--data-track") == 0) {
      options.data_track = true;
      options.mode_selected = true;
    } else if (std::strcmp(value, "--data-frames") == 0) {
      options.data_frames = true;
      options.mode_selected = true;
    } else if (std::strcmp(value, "--ffi-cycles") == 0) {
      options.ffi_cycles = true;
    } else if (std::strcmp(value, "--help") == 0 || std::strcmp(value, "-h") == 0) {
      printUsage(argv[0]);
      std::exit(0);
    } else if (argument == 1 && argc == 2) {
      options.iteration_count = parseIterationCount(value);
    } else {
      throw std::runtime_error(std::string("unknown argument: ") + value);
    }
  }

  if (!options.mode_selected) {
    options.sources = true;
    options.connect = true;
    options.media = true;
    options.data_track = true;
    options.data_frames = true;
  } else if (options.media) {
    options.connect = true;
  }
  if (options.data_frames) {
    options.data_track = true;
    options.connect = true;
  } else if (options.data_track) {
    options.connect = true;
  }
  return options;
}

void runIteration(const Configuration& config, const Options& options) {
  if (options.sources) {
    runUnusedSources();
  }
  if (options.connect) {
    runRoomIteration(config, options);
  }
}

void initializeSdk(int iteration) {
  if (!livekit::initialize(livekit::LogLevel::Warn)) {
    throw std::runtime_error("initialize failed at iteration " + std::to_string(iteration));
  }
}

} // namespace

int main(int argc, char* argv[]) {
  try {
    const Options options = parseOptions(argc, argv);
    const int progress_interval = options.iteration_count < 10 ? 1 : options.iteration_count / 10;
    const Configuration config =
        options.connect ? Configuration{requiredEnvironment("LIVEKIT_URL"), requiredEnvironment("LIVEKIT_TOKEN_A")}
                        : Configuration{};

    if (options.ffi_cycles) {
      std::cout << "Running " << options.iteration_count << " LiveKit initialize/work/shutdown cycles\n";
    } else {
      std::cout << "Running " << options.iteration_count << " workload cycles in one LiveKit SDK lifecycle\n";
      initializeSdk(1);
    }

    for (int iteration = 1; iteration <= options.iteration_count; ++iteration) {
      if (options.ffi_cycles) {
        initializeSdk(iteration);
      }

      try {
        runIteration(config, options);
      } catch (...) {
        livekit::shutdown();
        throw;
      }
      if (options.ffi_cycles) {
        livekit::shutdown();
      }

      if (iteration % progress_interval == 0 || iteration == options.iteration_count) {
        std::cout << "Completed " << iteration << "/" << options.iteration_count << " cycles\n";
      }
    }

    if (!options.ffi_cycles) {
      livekit::shutdown();
    }
  } catch (const std::exception& error) {
    std::cerr << "memory lifecycle tester failed: " << error.what() << '\n';
    printUsage(argv[0]);
    return 1;
  }

  return 0;
}
