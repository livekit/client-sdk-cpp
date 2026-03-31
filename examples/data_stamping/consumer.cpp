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
#include <csignal>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "data_alignment_manager.h"
#include "livekit/livekit.h"

using namespace livekit;
using livekit_examples::data_stamping::DataAlignmentManager;

namespace {

constexpr char kDataTrackName[] = "imu";
constexpr char kProducerParticipantId[] = "producer";

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

std::string getenvOrEmpty(const char *name) {
  const char *value = std::getenv(name);
  return value ? std::string(value) : std::string{};
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

  std::shared_ptr<DataAlignmentManager> alignment_manager =
      std::make_shared<DataAlignmentManager>(2'000'000);

  livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

  std::unique_ptr<Room> room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;

  LK_LOG_INFO("[consumer] connecting to {}", url);
  if (!room->Connect(url, token, options)) {
    LK_LOG_ERROR("[consumer] failed to connect");
    return 1;
  }
  LK_LOG_INFO("[consumer] connected as {} to room '{}'",
              room->localParticipant()->identity(), room->room_info().name);

  room->setOnVideoFrameEventCallback(
      kProducerParticipantId, TrackSource::SOURCE_CAMERA,
      [alignment_manager](const VideoFrameEvent &event) {
        if (!event.metadata || !event.metadata->user_timestamp_us.has_value()) {
          return;
        }
        alignment_manager->addVideoFrame(*event.metadata->user_timestamp_us);
      },
      VideoStream::Options{});

  room->addOnDataFrameCallback(
      kProducerParticipantId, kDataTrackName,
      [alignment_manager](const std::vector<std::uint8_t> &,
                          std::optional<std::uint64_t> user_timestamp) {
        if (!user_timestamp.has_value()) {
          return;
        }

        alignment_manager->addImuFrame(*user_timestamp);
      });

  while (g_running.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  livekit::shutdown();
  return 0;
}
