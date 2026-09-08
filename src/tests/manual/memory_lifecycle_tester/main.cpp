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
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <fstream>
#endif

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
constexpr int kReceiveVideoWidth = 640;
constexpr int kReceiveVideoHeight = 360;
constexpr int kReceiveRequiredFrames = 3;
constexpr int kReceiveCaptureAttempts = 250;
constexpr char kReceiveAudioTrackName[] = "lifecycle-receive-audio";
constexpr char kReceiveVideoTrackName[] = "lifecycle-receive-video";
constexpr char kMemoryLimitEnv[] = "LIVEKIT_MEMORY_MAX_FINAL_RSS_KIB";

struct Configuration {
  std::string url;
  std::string token;
  std::string receiver_token;
};

struct Options {
  int iteration_count{kDefaultIterations};
  bool sources{false};
  bool connect{false};
  bool media{false};
  bool data_track{false};
  bool data_frames{false};
  bool receive{false};
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

std::optional<std::uint64_t> parseMemoryLimitKib() {
  const char* value = std::getenv(kMemoryLimitEnv);
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  try {
    const unsigned long long parsed = std::stoull(value);
    if (parsed == 0) {
      throw std::runtime_error(std::string(kMemoryLimitEnv) + " must be greater than zero");
    }
    return static_cast<std::uint64_t>(parsed);
  } catch (const std::invalid_argument&) {
    throw std::runtime_error(std::string(kMemoryLimitEnv) + " must be an integer");
  } catch (const std::out_of_range&) {
    throw std::runtime_error(std::string(kMemoryLimitEnv) + " is out of range");
  }
}

std::optional<std::uint64_t> currentRssKib() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(counters.WorkingSetSize) / 1024;
#elif defined(__APPLE__)
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  const kern_return_t result =
      task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count);
  if (result != KERN_SUCCESS) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(info.resident_size) / 1024;
#else
  std::ifstream status("/proc/self/status");
  if (!status) {
    return std::nullopt;
  }
  std::string line;
  while (std::getline(status, line)) {
    if (line.compare(0, 6, "VmRSS:") != 0) {
      continue;
    }
    std::istringstream fields(line.substr(6));
    std::uint64_t kib = 0;
    fields >> kib;
    if (!fields) {
      return std::nullopt;
    }
    return kib;
  }
  return std::nullopt;
#endif
}

std::string formatRssKib(std::uint64_t rss_kib) {
  std::ostringstream stream;
  stream << rss_kib << " KiB (" << std::fixed << std::setprecision(2) << static_cast<double>(rss_kib) / 1024.0
         << " MiB)";
  return stream.str();
}

std::string formatRssSample() {
  const auto rss_kib = currentRssKib();
  if (!rss_kib) {
    return "unavailable";
  }
  return formatRssKib(*rss_kib);
}

void checkFinalRss() {
  const auto rss_kib = currentRssKib();
  if (rss_kib) {
    std::cout << "RSS final: " << formatRssKib(*rss_kib) << '\n';
  } else {
    std::cout << "RSS final: unavailable\n";
  }

  const auto limit_kib = parseMemoryLimitKib();
  if (!limit_kib) {
    return;
  }
  if (!rss_kib) {
    throw std::runtime_error(std::string(kMemoryLimitEnv) + " is set but RSS could not be sampled");
  }
  if (*rss_kib > *limit_kib) {
    throw std::runtime_error("final RSS " + formatRssKib(*rss_kib) + " exceeds " + kMemoryLimitEnv + " " +
                             formatRssKib(*limit_kib));
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

std::shared_ptr<livekit::LocalParticipant> requireLocalParticipant(livekit::Room& room) {
  auto participant = room.localParticipant().lock();
  if (!participant) {
    throw std::runtime_error("local participant is unavailable");
  }
  return participant;
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

void runReceiveWork(livekit::Room& sender_room, const Configuration& config) {
  livekit::Room receiver_room;
  std::mutex mutex;
  std::condition_variable frames_received;
  int audio_frames = 0;
  int video_frames = 0;

  const auto received_enough = [&audio_frames, &video_frames] {
    return audio_frames >= kReceiveRequiredFrames && video_frames >= kReceiveRequiredFrames;
  };

  try {
    if (!receiver_room.connect(config.url, config.receiver_token, {})) {
      throw std::runtime_error("receiver failed to connect to the LiveKit room");
    }

    const auto sender = requireLocalParticipant(sender_room);
    const std::string sender_identity = sender->identity();
    if (sender_identity.empty()) {
      throw std::runtime_error("sender identity is empty");
    }

    receiver_room.setOnAudioFrameCallback(sender_identity, kReceiveAudioTrackName,
                                          [&mutex, &frames_received, &audio_frames](const livekit::AudioFrame&) {
                                            std::lock_guard<std::mutex> lock(mutex);
                                            ++audio_frames;
                                            frames_received.notify_all();
                                          });
    receiver_room.setOnVideoFrameCallback(
        sender_identity, kReceiveVideoTrackName,
        [&mutex, &frames_received, &video_frames](const livekit::VideoFrame&, std::int64_t) {
          std::lock_guard<std::mutex> lock(mutex);
          ++video_frames;
          frames_received.notify_all();
        });

    auto audio_source = std::make_shared<livekit::AudioSource>(kAudioSampleRate, kAudioChannels, 0);
    auto audio_track = livekit::LocalAudioTrack::createLocalAudioTrack(kReceiveAudioTrackName, audio_source);
    if (!audio_track) {
      throw std::runtime_error("failed to create receive audio track");
    }
    auto video_source = std::make_shared<livekit::VideoSource>(kReceiveVideoWidth, kReceiveVideoHeight);
    auto video_track = livekit::LocalVideoTrack::createLocalVideoTrack(kReceiveVideoTrackName, video_source);
    if (!video_track) {
      throw std::runtime_error("failed to create receive video track");
    }

    livekit::TrackPublishOptions audio_options;
    audio_options.source = livekit::TrackSource::SOURCE_MICROPHONE;
    livekit::TrackPublishOptions video_options;
    video_options.source = livekit::TrackSource::SOURCE_CAMERA;
    video_options.simulcast = false;
    sender->publishTrack(audio_track, audio_options);
    sender->publishTrack(video_track, video_options);

    auto audio_frame =
        livekit::AudioFrame::create(kAudioSampleRate, kAudioChannels, kAudioSampleRate * kAudioFrameDurationMs / 1'000);
    auto video_frame =
        livekit::VideoFrame::create(kReceiveVideoWidth, kReceiveVideoHeight, livekit::VideoBufferType::I420);
    std::fill(video_frame.data(), video_frame.data() + video_frame.dataSize(), 0x7f);

    for (int attempt = 0; attempt < kReceiveCaptureAttempts; ++attempt) {
      audio_source->captureFrame(audio_frame, 20);
      video_source->captureFrame(video_frame);
      std::unique_lock<std::mutex> lock(mutex);
      if (frames_received.wait_for(lock, std::chrono::milliseconds(20), received_enough)) {
        break;
      }
    }

    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!received_enough()) {
        throw std::runtime_error("timed out waiting for the receiver to get audio and video frames");
      }
    }

    unpublishTrackIfPublished(sender, video_track);
    unpublishTrackIfPublished(sender, audio_track);
    receiver_room.clearOnVideoFrameCallback(sender_identity, kReceiveVideoTrackName);
    receiver_room.clearOnAudioFrameCallback(sender_identity, kReceiveAudioTrackName);
  } catch (...) {
    (void)receiver_room.disconnect();
    throw;
  }

  if (!receiver_room.disconnect()) {
    throw std::runtime_error("failed to disconnect the receiver from the LiveKit room");
  }
}

void runRoomIteration(const Configuration& config, const Options& options) {
  livekit::Room room;
  if (!room.connect(config.url, config.token, {})) {
    throw std::runtime_error("failed to connect to the LiveKit room");
  }

  try {
    auto participant = requireLocalParticipant(room);

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

    if (options.receive) {
      runReceiveWork(room, config);
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
            << " [--iterations N] [--ffi-cycles] [--sources] [--connect] [--media] [--data-track] "
               "[--data-frames] [--receive]\n"
            << "  --ffi-cycles   Initialize and shut down the SDK on every iteration.\n"
            << "  --sources      Create and drop unused local audio/video sources and tracks.\n"
            << "  --connect      Connect to and leave a room.\n"
            << "  --media        Publish, capture, and unpublish audio/video tracks (implies --connect).\n"
            << "  --data-track   Publish and unpublish a data track (implies --connect).\n"
            << "  --data-frames  Send data frames (implies --data-track and --connect).\n"
            << "  --receive      Subscribe to synthetic audio/video from a second participant "
               "(implies --connect).\n"
            << "  By default the SDK is initialized once and all workloads except --receive run on every "
               "iteration.\n"
            << "  A single numeric argument remains supported as the iteration count.\n"
            << "  Set " << kMemoryLimitEnv << " to fail if final RSS exceeds that many KiB.\n";
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
    } else if (std::strcmp(value, "--receive") == 0) {
      options.receive = true;
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
  if (options.receive) {
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

Configuration loadConfiguration(const Options& options) {
  Configuration config;
  if (!options.connect) {
    return config;
  }
  config.url = requiredEnvironment("LIVEKIT_URL");
  config.token = requiredEnvironment("LIVEKIT_TOKEN_A");
  if (options.receive) {
    config.receiver_token = requiredEnvironment("LIVEKIT_TOKEN_B");
  }
  return config;
}

} // namespace

int main(int argc, char* argv[]) {
  try {
    const Options options = parseOptions(argc, argv);
    const int progress_interval = options.iteration_count < 10 ? 1 : options.iteration_count / 10;
    const Configuration config = loadConfiguration(options);

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
        std::cout << "Completed " << iteration << "/" << options.iteration_count << " cycles (RSS " << formatRssSample()
                  << ")\n";
      }
    }

    if (!options.ffi_cycles) {
      try {
        checkFinalRss();
      } catch (...) {
        livekit::shutdown();
        throw;
      }
      livekit::shutdown();
    } else {
      checkFinalRss();
    }
  } catch (const std::exception& error) {
    std::cerr << "memory lifecycle tester failed: " << error.what() << '\n';
    printUsage(argv[0]);
    return 1;
  }

  return 0;
}
