/*
 * Copyright 2025 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/// Consumer participant: prints each incoming message on the `producer-status`
/// text stream topic. Use a token whose identity is `consumer`.

#include "livekit/livekit.h"
#include "livekit/lk_log.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

using namespace livekit;

namespace {

constexpr const char *kTopic = "producer-status";

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

std::string getenvOrEmpty(const char *name) {
  const char *v = std::getenv(name);
  return v ? std::string(v) : std::string{};
}

void handleStatusMessage(std::shared_ptr<TextStreamReader> reader,
                         const std::string &participant_identity) {
  try {
    const std::string text = reader->readAll();
    LK_LOG_INFO("[from {}] {}", participant_identity, text);
  } catch (const std::exception &e) {
    LK_LOG_ERROR("Error reading text stream from {}: {}", participant_identity,
                 e.what());
  }
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

  LK_LOG_INFO("consumer connected as identity='{}' room='{}'",
              lp->identity(), room->room_info().name);

  room->registerTextStreamHandler(
      kTopic, [](std::shared_ptr<TextStreamReader> reader,
                 const std::string &participant_identity) {
        std::thread t(handleStatusMessage, std::move(reader),
                      participant_identity);
        t.detach();
      });

  LK_LOG_INFO("listening on topic '{}'; Ctrl-C to exit", kTopic);

  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  LK_LOG_INFO("shutting down");
  room->unregisterTextStreamHandler(kTopic);
  room->setDelegate(nullptr);
  room.reset();
  livekit::shutdown();
  return 0;
}
