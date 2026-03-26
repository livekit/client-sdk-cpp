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

/// Producer participant: publishes a data track named "status" and pushes
/// periodic binary status frames (4 Hz). Use a token whose identity is
/// `producer`.

#include "livekit/livekit.h"
#include "livekit/lk_log.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace livekit;

namespace {

constexpr const char *kTrackName = "status";

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

std::string getenvOrEmpty(const char *name) {
  const char *v = std::getenv(name);
  return v ? std::string(v) : std::string{};
}

} // namespace

int main(int argc, char *argv[]) {
  std::string url = getenvOrEmpty("LIVEKIT_URL");
  std::string token = getenvOrEmpty("LIVEKIT_TOKEN");

  if (argc >= 3) {
    url = argv[1];
    token = argv[2];
  }

  if (url.empty() || token.empty()) {
    LK_LOG_ERROR("LIVEKIT_URL and LIVEKIT_TOKEN (or <ws-url> <token>) are "
                 "required");
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

  if (!room->Connect(url, token, options)) {
    LK_LOG_ERROR("Failed to connect to room");
    livekit::shutdown();
    return 1;
  }

  LocalParticipant *lp = room->localParticipant();
  if (!lp) {
    LK_LOG_ERROR("No local participant after connect");
    room->setDelegate(nullptr);
    room.reset();
    livekit::shutdown();
    return 1;
  }

  LK_LOG_INFO("producer connected as identity='{}' room='{}'", lp->identity(),
              room->room_info().name);

  std::shared_ptr<LocalDataTrack> data_track;
  try {
    data_track = lp->publishDataTrack(kTrackName);
  } catch (const std::exception &e) {
    LK_LOG_ERROR("Failed to publish data track: {}", e.what());
    room->setDelegate(nullptr);
    room.reset();
    livekit::shutdown();
    return 1;
  }

  LK_LOG_INFO("published data track '{}'", kTrackName);

  using clock = std::chrono::steady_clock;
  const auto start = clock::now();
  const auto period = std::chrono::milliseconds(250);
  auto next_deadline = clock::now();
  std::uint64_t count = 0;

  while (g_running.load()) {
    const auto now = clock::now();
    const double elapsed_sec =
        std::chrono::duration<double>(now - start).count();

    std::ostringstream body;
    body << std::fixed << std::setprecision(2) << elapsed_sec;
    const std::string text = std::string("[time-since-start]: ") + body.str() +
                             " count: " + std::to_string(count);

    std::vector<std::uint8_t> payload(text.begin(), text.end());
    if (!data_track->tryPush(payload)) {
      LK_LOG_WARN("Failed to push data frame");
    }

    LK_LOG_DEBUG("sent: {}", text);
    ++count;

    next_deadline += period;
    std::this_thread::sleep_until(next_deadline);
  }

  LK_LOG_INFO("shutting down");
  data_track->unpublishDataTrack();
  room->setDelegate(nullptr);
  room.reset();
  livekit::shutdown();
  return 0;
}
