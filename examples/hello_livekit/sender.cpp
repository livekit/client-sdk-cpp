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

/// Publishes synthetic RGBA video and a data track. Run the receiver in another
/// process and pass this participant's identity (printed after connect).
///
/// Usage:
///   HelloLivekitSender <ws-url> <sender-token>
///
/// Or via environment variables:
///   LIVEKIT_URL, LIVEKIT_SENDER_TOKEN

#include "livekit/livekit.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <thread>

using namespace livekit;

constexpr int kWidth = 640;
constexpr int kHeight = 480;
constexpr const char *kVideoTrackName = "camera0";
constexpr const char *kDataTrackName = "app-data";

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

std::string getenvOrEmpty(const char *name) {
  const char *v = std::getenv(name);
  return v ? std::string(v) : std::string{};
}

int main(int argc, char *argv[]) {
  std::string url = getenvOrEmpty("LIVEKIT_URL");
  std::string sender_token = getenvOrEmpty("LIVEKIT_SENDER_TOKEN");

  if (argc >= 3) {
    url = argv[1];
    sender_token = argv[2];
  }

  if (url.empty() || sender_token.empty()) {
    LK_LOG_ERROR("Usage: HelloLivekitSender <ws-url> <sender-token>\n"
                 "  or set LIVEKIT_URL, LIVEKIT_SENDER_TOKEN");
    return 1;
  }

  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif

  livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

  auto room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;

  if (!room->Connect(url, sender_token, options)) {
    LK_LOG_ERROR("[sender] Failed to connect");
    livekit::shutdown();
    return 1;
  }

  LocalParticipant *lp = room->localParticipant();
  assert(lp);

  LK_LOG_INFO("[sender] Connected as identity='{}' room='{}' — pass this "
              "identity to HelloLivekitReceiver",
              lp->identity(), room->room_info().name);

  auto video_source = std::make_shared<VideoSource>(kWidth, kHeight);

  std::shared_ptr<LocalVideoTrack> video_track = lp->publishVideoTrack(
      kVideoTrackName, video_source, TrackSource::SOURCE_CAMERA);

  auto publish_result = lp->publishDataTrack(kDataTrackName);
  if (!publish_result) {
    const auto &error = publish_result.error();
    LK_LOG_ERROR("Failed to publish data track: code={} message={}",
                 static_cast<std::uint32_t>(error.code), error.message);
    room.reset();
    livekit::shutdown();
    return 1;
  }
  std::shared_ptr<LocalDataTrack> data_track = publish_result.value();

  const auto t0 = std::chrono::steady_clock::now();
  std::uint64_t count = 0;

  LK_LOG_INFO(
      "[sender] Publishing synthetic video + data on '{}'; Ctrl-C to exit",
      kDataTrackName);

  while (g_running.load()) {
    VideoFrame vf = VideoFrame::create(kWidth, kHeight, VideoBufferType::RGBA);
    video_source->captureFrame(std::move(vf));

    const auto now = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(now - t0).count();
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << ms << " ms, count: " << count;
    const std::string msg = oss.str();
    auto push_result =
        data_track->tryPush(std::vector<std::uint8_t>(msg.begin(), msg.end()));
    if (!push_result) {
      const auto &error = push_result.error();
      LK_LOG_WARN("Failed to push data frame: code={} message={}",
                  static_cast<std::uint32_t>(error.code), error.message);
    }

    ++count;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  LK_LOG_INFO("[sender] Disconnecting");
  room.reset();

  livekit::shutdown();
  return 0;
}
