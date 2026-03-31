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

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "livekit/livekit.h"

using namespace livekit;

namespace {

constexpr char kVideoTrackName[] = "stamped-camera";
constexpr char kDataTrackName[] = "imu";
constexpr int kFrameWidth = 640;
constexpr int kFrameHeight = 360;
constexpr int kVideoIntervalMs = 33; // ~30Hz
constexpr int kDataIntervalMs = 10;  // 100Hz
constexpr double kWaveHz = 0.5;
constexpr double kAngularAmplitude = 1.0;
constexpr double kLinearAmplitude = 2.0;

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

std::string getenvOrEmpty(const char *name) {
  const char *value = std::getenv(name);
  return value ? std::string(value) : std::string{};
}

std::uint64_t nowEpochUs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

double sampleWave(double elapsed_seconds, double phase) {
  constexpr double kTau = 6.28318530717958647692;
  return std::sin((kTau * kWaveHz * elapsed_seconds) + phase);
}

std::string buildImuJson(double elapsed_seconds) {
  const double angular_x = kAngularAmplitude * sampleWave(elapsed_seconds, 0.0);
  const double angular_y =
      kAngularAmplitude * sampleWave(elapsed_seconds, 2.09439510239);
  const double angular_z =
      kAngularAmplitude * sampleWave(elapsed_seconds, 4.18879020479);

  const double linear_x = kLinearAmplitude * sampleWave(elapsed_seconds, 0.5);
  const double linear_y = kLinearAmplitude * sampleWave(elapsed_seconds, 1.5);
  const double linear_z = kLinearAmplitude * sampleWave(elapsed_seconds, 2.5);

  std::ostringstream stream;
  stream << std::fixed << std::setprecision(4) << "{"
         << "\"angular\":{\"x\":" << angular_x << ",\"y\":" << angular_y
         << ",\"z\":" << angular_z << "},"
         << "\"linear\":{\"x\":" << linear_x << ",\"y\":" << linear_y
         << ",\"z\":" << linear_z << "}"
         << "}";
  return stream.str();
}

void fillFrame(VideoFrame &frame, double elapsed_seconds) {
  const double brightness = 0.5 + 0.5 * sampleWave(elapsed_seconds, 0.0);
  const std::uint8_t blue =
      static_cast<std::uint8_t>(40 + (brightness * 120.0));
  const std::uint8_t green =
      static_cast<std::uint8_t>(60 + (brightness * 140.0));
  const std::uint8_t red = static_cast<std::uint8_t>(80 + (brightness * 100.0));

  std::uint8_t *data = frame.data();
  for (std::size_t i = 0; i < frame.dataSize(); i += 4) {
    data[i + 0] = blue;
    data[i + 1] = green;
    data[i + 2] = red;
    data[i + 3] = 255;
  }
}

void printUsage(const char *program) {
  LK_LOG_INFO("Usage:\n  {} <ws-url> <token>\nor:\n  LIVEKIT_URL=... "
              "LIVEKIT_TOKEN=... {}",
              program, program);
}

bool parseArgs(int argc, char *argv[], std::string &url, std::string &token) {
  if (argc > 1) {
    const std::string first = argv[1];
    if (first == "-h" || first == "--help") {
      return false;
    }
  }

  url = getenvOrEmpty("LIVEKIT_URL");
  token = getenvOrEmpty("LIVEKIT_TOKEN");

  if (argc >= 3) {
    url = argv[1];
    token = argv[2];
  }

  return !(url.empty() || token.empty());
}

void runVideoPublisher(
    const std::shared_ptr<VideoSource> &video_source,
    const std::chrono::steady_clock::time_point &start_time) {

  uint64_t frame_count = 0;
  VideoFrame frame =
      VideoFrame::create(kFrameWidth, kFrameHeight, VideoBufferType::BGRA);
  auto next_frame_at = std::chrono::steady_clock::now();

  while (g_running.load(std::memory_order_relaxed)) {
    const auto now = std::chrono::steady_clock::now();
    const double elapsed_seconds =
        std::chrono::duration<double>(now - start_time).count();

    fillFrame(frame, elapsed_seconds);

    VideoCaptureOptions capture_options;
    capture_options.timestamp_us = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now - start_time)
            .count());
    capture_options.rotation = VideoRotation::VIDEO_ROTATION_0;
    capture_options.metadata = VideoFrameMetadata{};
    capture_options.metadata->user_timestamp_us = nowEpochUs();

    video_source->captureFrame(frame, capture_options);
    if (frame_count % 300 == 0) {
      LK_LOG_INFO("[producer] captured frame {} timestamp={} user_timestamp={}",
                  frame_count, capture_options.timestamp_us,
                  capture_options.metadata->user_timestamp_us.value_or(0));
    }
    ++frame_count;
    next_frame_at += std::chrono::milliseconds(kVideoIntervalMs);
    std::this_thread::sleep_until(next_frame_at);
  }
}

void runImuPublisher(const std::shared_ptr<LocalDataTrack> &data_track,
                     const std::chrono::steady_clock::time_point &start_time) {
  uint64_t frame_count = 0;
  auto next_frame_at = std::chrono::steady_clock::now();

  while (g_running.load(std::memory_order_relaxed)) {
    const auto now = std::chrono::steady_clock::now();
    const double elapsed_seconds =
        std::chrono::duration<double>(now - start_time).count();

    const std::string payload = buildImuJson(elapsed_seconds);
    auto push_result = data_track->tryPush(
        std::vector<std::uint8_t>(payload.begin(), payload.end()),
        nowEpochUs());
    if (!push_result) {
      const auto &error = push_result.error();
      LK_LOG_ERROR("[producer] failed to push data frame: {}", error.message);
    }

    if (frame_count % 1000 == 0) {
      LK_LOG_INFO("[producer] pushed IMU frame {} timestamp={}", frame_count,
                  nowEpochUs());
    }
    ++frame_count;
    next_frame_at += std::chrono::milliseconds(kDataIntervalMs);
    std::this_thread::sleep_until(next_frame_at);
  }
}

} // namespace

int main(int argc, char *argv[]) {
  std::string url;
  std::string token;

  if (!parseArgs(argc, argv, url, token)) {
    printUsage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif

  livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

  std::unique_ptr<Room> room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;

  LK_LOG_INFO("[producer] connecting to {}", url);
  if (!room->Connect(url, token, options)) {
    LK_LOG_ERROR("[producer] failed to connect");
    livekit::shutdown();
    return 1;
  }

  auto *participant = room->localParticipant();
  assert(participant != nullptr);
  LK_LOG_INFO("[producer] connected as {} to room '{}'",
              participant->identity(), room->room_info().name);

  auto video_source = std::make_shared<VideoSource>(kFrameWidth, kFrameHeight);
  auto video_track =
      LocalVideoTrack::createLocalVideoTrack(kVideoTrackName, video_source);

  auto data_track_result = participant->publishDataTrack(kDataTrackName);
  if (!data_track_result) {
    const auto &error = data_track_result.error();
    LK_LOG_ERROR("[producer] failed to publish data track: {}", error.message);
    return 1;
  }
  auto data_track = data_track_result.value();

  try {
    TrackPublishOptions publish_options;
    publish_options.source = TrackSource::SOURCE_CAMERA;
    publish_options.packet_trailer_features.user_timestamp = true;
    participant->publishTrack(video_track, publish_options);

    LK_LOG_INFO("[producer] publishing stamped video track '{}' and data track "
                "'{}'",
                kVideoTrackName, kDataTrackName);

    const auto steady_start = std::chrono::steady_clock::now();
    std::thread video_thread(runVideoPublisher, video_source, steady_start);
    std::thread imu_thread(runImuPublisher, data_track, steady_start);

    video_thread.join();
    imu_thread.join();
  } catch (const std::exception &error) {
    LK_LOG_ERROR("[producer] error: {}", error.what());
    return 1;
  }

  room.reset(); // reset the room to cleanup pubs/subs/rpc/etc before shutdown
  livekit::shutdown();
  return 0;
}
