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
#include "messages.h"
#include "utils.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
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
    std::cerr << "LIVEKIT_URL and LIVEKIT_TOKEN (or <ws-url> <token>) are "
                 "required\n";
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
    std::cerr << "Failed to connect to room\n";
    livekit::shutdown();
    return 1;
  }

  LocalParticipant *local_participant = room->localParticipant();
  assert(local_participant);

  std::cout << "ping connected as identity='" << local_participant->identity()
            << "' room='" << room->room_info().name << "'\n";

  auto publish_result =
      local_participant->publishDataTrack(ping_pong::kPingTrackName);
  if (!publish_result) {
    const auto &error = publish_result.error();
    std::cerr << "Failed to publish ping data track: code="
              << static_cast<std::uint32_t>(error.code)
              << " message=" << error.message << "\n";
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
              std::cerr << "Received pong for unknown id=" << pong_message.rec_id
                        << "\n";
              return;
            }
            ping_message = it->second;
            sent_messages.erase(it);
          }

          const auto metrics = calculateLatencyMetrics(
              ping_message, pong_message, received_ts_ns);

          std::cout << "pong id=" << metrics.id << " rtt_ms=" << std::fixed
                    << std::setprecision(3) << metrics.round_trip_time_ms
                    << " pong_to_ping_ms=" << metrics.pong_to_ping_time_ms
                    << " ping_to_pong_and_processing_ms="
                    << metrics.ping_to_pong_and_processing_ms
                    << " estimated_one_way_latency_ms="
                    << metrics.estimated_one_way_latency_ms << "\n";
        } catch (const std::exception &e) {
          std::cerr << "Failed to process pong payload: " << e.what() << "\n";
        }
      });

  std::cout << "published data track '" << ping_pong::kPingTrackName
            << "' and listening for '" << ping_pong::kPongTrackName
            << "' from '" << ping_pong::kPongParticipantIdentity << "'\n";

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
      std::cerr << "Failed to push ping data frame: code="
                << static_cast<std::uint32_t>(error.code)
                << " message=" << error.message << "\n";
    } else {
      {
        std::lock_guard<std::mutex> lock(sent_messages_mutex);
        sent_messages.emplace(ping_message.id, ping_message);
      }
      std::cout << "sent ping id=" << ping_message.id
                << " ts_ns=" << ping_message.ts_ns << "\n";
    }

    next_deadline += ping_pong::kPingPeriod;
    std::this_thread::sleep_until(next_deadline);
  }

  std::cout << "shutting down ping participant\n";
  room.reset();
  livekit::shutdown();
  return 0;
}
