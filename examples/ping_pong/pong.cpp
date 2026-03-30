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

/// Pong participant: listens on the "ping" data track and publishes responses
/// on the "pong" data track. Use a token whose identity is `pong`.

#include "constants.h"
#include "json_converters.h"
#include "livekit/livekit.h"
#include "livekit/lk_log.h"
#include "messages.h"
#include "utils.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace livekit;

namespace {

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

} // namespace

int main(int argc, char *argv[]) {
  std::string url = ping_pong::getenvOrEmpty("LIVEKIT_URL");
  std::string token = ping_pong::getenvOrEmpty("LIVEKIT_TOKEN");

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

  LocalParticipant *local_participant = room->localParticipant();
  assert(local_participant);

  LK_LOG_INFO("pong connected as identity='{}' room='{}'",
              local_participant->identity(), room->room_info().name);

  auto publish_result =
      local_participant->publishDataTrack(ping_pong::kPongTrackName);
  if (!publish_result) {
    const auto &error = publish_result.error();
    LK_LOG_ERROR("Failed to publish pong data track: code={} retryable={} "
                 "message={}",
                 static_cast<std::uint32_t>(error.code), error.retryable,
                 error.message);
    room->setDelegate(nullptr);
    room.reset();
    livekit::shutdown();
    return 1;
  }

  std::shared_ptr<LocalDataTrack> pong_track = publish_result.value();

  const auto callback_id = room->addOnDataFrameCallback(
      ping_pong::kPingParticipantIdentity, ping_pong::kPingTrackName,
      [pong_track](const std::vector<std::uint8_t> &payload,
                   std::optional<std::uint64_t> /*user_timestamp*/) {
        try {
          if (payload.empty()) {
            LK_LOG_DEBUG("Ignoring empty ping payload");
            return;
          }

          const auto ping_message =
              ping_pong::pingMessageFromJson(ping_pong::toString(payload));

          ping_pong::PongMessage pong_message;
          pong_message.rec_id = ping_message.id;
          pong_message.ts_ns = ping_pong::timeSinceEpochNs();

          const std::string json = ping_pong::pongMessageToJson(pong_message);
          auto push_result = pong_track->tryPush(ping_pong::toPayload(json));
          if (!push_result) {
            const auto &error = push_result.error();
            LK_LOG_WARN("Failed to push pong data frame: code={} retryable={} "
                        "message={}",
                        static_cast<std::uint32_t>(error.code),
                        error.retryable, error.message);
            return;
          }

          LK_LOG_INFO("received ping id={} ts_ns={} and sent pong rec_id={} "
                      "ts_ns={}",
                      ping_message.id, ping_message.ts_ns, pong_message.rec_id,
                      pong_message.ts_ns);
        } catch (const std::exception &e) {
          LK_LOG_WARN("Failed to process ping payload: {}", e.what());
        }
      });

  LK_LOG_INFO("published data track '{}' and listening for '{}' from '{}'",
              ping_pong::kPongTrackName, ping_pong::kPingTrackName,
              ping_pong::kPingParticipantIdentity);

  while (g_running.load()) {
    std::this_thread::sleep_for(ping_pong::kPollPeriod);
  }

  LK_LOG_INFO("shutting down pong participant");
  room.reset();
  livekit::shutdown();
  return 0;
}
