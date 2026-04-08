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

/// Subscribes to the sender's camera video and data track. Run
/// HelloLivekitSender first; use the identity it prints, or the sender's known
/// participant name.
///
/// Usage:
///   HelloLivekitReceiver <ws-url> <receiver-token> <sender-identity>
///
/// Or via environment variables:
///   LIVEKIT_URL, LIVEKIT_RECEIVER_TOKEN, LIVEKIT_SENDER_IDENTITY

#include "livekit/livekit.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace livekit;

constexpr const char *kDataTrackName = "app-data";
constexpr const char *kVideoTrackName = "camera0";

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

std::string getenvOrEmpty(const char *name) {
  const char *v = std::getenv(name);
  return v ? std::string(v) : std::string{};
}

int main(int argc, char *argv[]) {
  std::string url = getenvOrEmpty("LIVEKIT_URL");
  std::string receiver_token = getenvOrEmpty("LIVEKIT_RECEIVER_TOKEN");
  std::string sender_identity = getenvOrEmpty("LIVEKIT_SENDER_IDENTITY");

  if (argc >= 4) {
    url = argv[1];
    receiver_token = argv[2];
    sender_identity = argv[3];
  }

  if (url.empty() || receiver_token.empty() || sender_identity.empty()) {
    std::cerr << "Usage: HelloLivekitReceiver <ws-url> <receiver-token> "
                 "<sender-identity>\n"
                 "  or set LIVEKIT_URL, LIVEKIT_RECEIVER_TOKEN, "
                 "LIVEKIT_SENDER_IDENTITY\n";
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

  if (!room->Connect(url, receiver_token, options)) {
    std::cerr << "[receiver] Failed to connect\n";
    livekit::shutdown();
    return 1;
  }

  LocalParticipant *lp = room->localParticipant();
  assert(lp);

  std::cout << "[receiver] Connected as identity='" << lp->identity()
            << "' room='" << room->room_info().name
            << "'; subscribing to sender identity='" << sender_identity
            << "'\n";

  int video_frame_count = 0;
  room->setOnVideoFrameCallback(
      sender_identity, kVideoTrackName,
      [&video_frame_count](const VideoFrame &frame, std::int64_t timestamp_us) {
        const auto ts_ms =
            std::chrono::duration<double, std::milli>(timestamp_us).count();
        const int n = video_frame_count++;
        if (n % 10 == 0) {
          std::cout << "[receiver] Video frame #" << n << " " << frame.width()
                    << "x" << frame.height() << " ts_ms=" << ts_ms << "\n";
        }
      });

  int data_frame_count = 0;
  room->addOnDataFrameCallback(
      sender_identity, kDataTrackName,
      [&data_frame_count](const std::vector<std::uint8_t> &payload,
                          std::optional<std::uint64_t> user_ts) {
        const int n = data_frame_count++;
        if (n % 10 == 0) {
          std::cout << "[receiver] Data frame #" << n << "\n";
        }
      });

  std::cout << "[receiver] Listening for video track '" << kVideoTrackName
            << "' + data track '" << kDataTrackName << "'; Ctrl-C to exit\n";

  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  std::cout << "[receiver] Shutting down\n";
  room.reset();

  livekit::shutdown();
  return 0;
}
