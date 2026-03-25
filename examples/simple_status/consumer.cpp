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

/// Consumer participant: creates 3 independent data track subscriptions to the
/// producer's "status" data track and logs each frame with the subscriber
/// index. Use a token whose identity is `consumer`.

#include "livekit/livekit.h"
#include "livekit/lk_log.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace livekit;

namespace {

constexpr const char *kProducerIdentity = "producer";
constexpr const char *kTrackName = "status";
constexpr int kNumSubscribers = 3;

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

  LK_LOG_INFO("consumer connected as identity='{}' room='{}'", lp->identity(),
              room->room_info().name);

  std::vector<DataFrameCallbackId> sub_ids;
  sub_ids.reserve(kNumSubscribers);

  for (int i = 0; i < kNumSubscribers; ++i) {
    auto id = room->addOnDataFrameCallback(
        kProducerIdentity, kTrackName,
        [i](const std::vector<std::uint8_t> &payload,
            std::optional<std::uint64_t> /*user_timestamp*/) {
          std::string text(payload.begin(), payload.end());
          LK_LOG_INFO("[subscriber {}] {}", i, text);
        });
    sub_ids.push_back(id);
    LK_LOG_INFO("registered subscriber {} (id={})", i, id);
  }

  LK_LOG_INFO("listening for data track '{}' from '{}' with {} subscribers; "
              "Ctrl-C to exit",
              kTrackName, kProducerIdentity, kNumSubscribers);

  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  LK_LOG_INFO("shutting down");
  for (auto id : sub_ids) {
    room->removeOnDataFrameCallback(id);
  }
  room->setDelegate(nullptr);
  room.reset();
  livekit::shutdown();
  return 0;
}
