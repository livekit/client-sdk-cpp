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

/// Ping participant: publishes on the "ping" data track, listens on "pong",
/// and logs latency metrics for each matched response. Use a token whose
/// identity is `ping`.

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
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace livekit;

namespace {

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

ping_pong::LatencyMetrics
calculateLatencyMetrics(const ping_pong::PingMessage &ping_message,
                        const ping_pong::PongMessage &pong_message,
                        std::int64_t received_ts_ns) {
  ping_pong::LatencyMetrics metrics;
  metrics.id = ping_message.id;
  metrics.pong_sent_ts_ns = pong_message.ts_ns;
  metrics.ping_received_ts_ns = received_ts_ns;
  metrics.round_trip_time_ns = received_ts_ns - ping_message.ts_ns;
  metrics.pong_to_ping_time_ns = received_ts_ns - pong_message.ts_ns;
  metrics.ping_to_pong_and_processing_ns =
      pong_message.ts_ns - ping_message.ts_ns;
  metrics.estimated_one_way_latency_ns =
      static_cast<double>(metrics.round_trip_time_ns) / 2.0;
  metrics.round_trip_time_ms =
      static_cast<double>(metrics.round_trip_time_ns) / 1'000'000.0;
  metrics.pong_to_ping_time_ms =
      static_cast<double>(metrics.pong_to_ping_time_ns) / 1'000'000.0;
  metrics.ping_to_pong_and_processing_ms =
      static_cast<double>(metrics.ping_to_pong_and_processing_ns) / 1'000'000.0;
  metrics.estimated_one_way_latency_ms =
      metrics.estimated_one_way_latency_ns / 1'000'000.0;
  return metrics;
}

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

  LK_LOG_INFO("ping connected as identity='{}' room='{}'",
              local_participant->identity(), room->room_info().name);

  auto publish_result =
      local_participant->publishDataTrack(ping_pong::kPingTrackName);
  if (!publish_result) {
    const auto &error = publish_result.error();
    LK_LOG_ERROR("Failed to publish ping data track: code={} message={}",
                 static_cast<std::uint32_t>(error.code), error.message);
    room->setDelegate(nullptr);
    room.reset();
    livekit::shutdown();
    return 1;
  }

  std::shared_ptr<LocalDataTrack> ping_track = publish_result.value();
  std::unordered_map<std::uint64_t, ping_pong::PingMessage> sent_messages;
  std::mutex sent_messages_mutex;

  const auto callback_id = room->addOnDataFrameCallback(
      ping_pong::kPongParticipantIdentity, ping_pong::kPongTrackName,
      [&sent_messages,
       &sent_messages_mutex](const std::vector<std::uint8_t> &payload,
                             std::optional<std::uint64_t> /*user_timestamp*/) {
        try {
          if (payload.empty()) {
            LK_LOG_DEBUG("Ignoring empty pong payload");
            return;
          }

          const auto pong_message =
              ping_pong::pongMessageFromJson(ping_pong::toString(payload));
          const auto received_ts_ns = ping_pong::timeSinceEpochNs();

          ping_pong::PingMessage ping_message;
          {
            std::lock_guard<std::mutex> lock(sent_messages_mutex);
            const auto it = sent_messages.find(pong_message.rec_id);
            if (it == sent_messages.end()) {
              LK_LOG_WARN("Received pong for unknown id={}",
                          pong_message.rec_id);
              return;
            }
            ping_message = it->second;
            sent_messages.erase(it);
          }

          const auto metrics = calculateLatencyMetrics(
              ping_message, pong_message, received_ts_ns);

          LK_LOG_INFO("pong id={} rtt_ms={:.3f} "
                      "pong_to_ping_ms={:.3f} "
                      "ping_to_pong_and_processing_ms={:.3f} "
                      "estimated_one_way_latency_ms={:.3f}",
                      metrics.id, metrics.round_trip_time_ms,
                      metrics.pong_to_ping_time_ms,
                      metrics.ping_to_pong_and_processing_ms,
                      metrics.estimated_one_way_latency_ms);
        } catch (const std::exception &e) {
          LK_LOG_WARN("Failed to process pong payload: {}", e.what());
        }
      });

  LK_LOG_INFO("published data track '{}' and listening for '{}' from '{}'",
              ping_pong::kPingTrackName, ping_pong::kPongTrackName,
              ping_pong::kPongParticipantIdentity);

  std::uint64_t next_id = 1;
  auto next_deadline = std::chrono::steady_clock::now();

  while (g_running.load()) {
    ping_pong::PingMessage ping_message;
    ping_message.id = next_id++;
    ping_message.ts_ns = ping_pong::timeSinceEpochNs();

    const std::string json = ping_pong::pingMessageToJson(ping_message);
    auto push_result = ping_track->tryPush(ping_pong::toPayload(json));
    if (!push_result) {
      const auto &error = push_result.error();
      LK_LOG_WARN("Failed to push ping data frame: code={} message={}",
                  static_cast<std::uint32_t>(error.code), error.message);
    } else {
      {
        std::lock_guard<std::mutex> lock(sent_messages_mutex);
        sent_messages.emplace(ping_message.id, ping_message);
      }
      LK_LOG_INFO("sent ping id={} ts_ns={}", ping_message.id,
                  ping_message.ts_ns);
    }

    next_deadline += ping_pong::kPingPeriod;
    std::this_thread::sleep_until(next_deadline);
  }

  LK_LOG_INFO("shutting down ping participant");
  room.reset();
  livekit::shutdown();
  return 0;
}
