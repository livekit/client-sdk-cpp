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
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#include "json_utils.h"
#include "livekit/livekit.h"
#include "utils.h"

using namespace livekit;
using namespace std::chrono_literals;

namespace {

std::atomic<bool> g_running{true};
std::atomic<bool> g_sender_connected{false};

void handleSignal(int) { g_running.store(false); }

void printUsage(const char *prog) {
  std::cerr << "Usage:\n"
            << "  " << prog << " <ws-url> <token>\n"
            << "or:\n"
            << "  " << prog << " --url=<ws-url> --token=<token>\n\n"
            << "Env fallbacks:\n"
            << "  LIVEKIT_URL, LIVEKIT_TOKEN\n\n"
            << "This is the receiver. It waits for a sender peer to\n"
            << "connect and send joystick commands via RPC.\n"
            << "Exits after 2 minutes if no commands are received.\n";
}

} // namespace

int main(int argc, char *argv[]) {
  std::string url, token;
  if (!simple_joystick::parseArgs(argc, argv, url, token)) {
    printUsage(argv[0]);
    return 1;
  }

  std::cout << "[Receiver] Connecting to: " << url << "\n";
  std::signal(SIGINT, handleSignal);

  livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);
  auto room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;

  bool res = room->Connect(url, token, options);
  std::cout << "[Receiver] Connect result: " << std::boolalpha << res << "\n";
  if (!res) {
    std::cerr << "[Receiver] Failed to connect to room\n";
    livekit::shutdown();
    return 1;
  }

  auto info = room->room_info();
  std::cout << "[Receiver] Connected to room: " << info.name << "\n";
  std::cout << "[Receiver] Waiting for sender peer (up to 2 minutes)...\n";

  // Register RPC handler for joystick commands
  LocalParticipant *lp = room->localParticipant();
  lp->registerRpcMethod(
      "joystick_command",
      [](const RpcInvocationData &data) -> std::optional<std::string> {
        try {
          auto cmd = simple_joystick::json_to_joystick(data.payload);
          g_sender_connected.store(true);
          std::cout << "[Receiver] Joystick from '" << data.caller_identity
                    << "': x=" << cmd.x << " y=" << cmd.y << " z=" << cmd.z
                    << "\n";
          return std::optional<std::string>{"ok"};
        } catch (const std::exception &e) {
          std::cerr << "[Receiver] Bad joystick payload: " << e.what() << "\n";
          throw;
        }
      });

  std::cout << "[Receiver] RPC handler 'joystick_command' registered. "
            << "Listening for commands...\n";

  // Wait up to 2 minutes for activity, then exit as failure
  auto deadline = std::chrono::steady_clock::now() + 2min;

  while (g_running.load() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(100ms);
  }

  if (!g_running.load()) {
    std::cout << "[Receiver] Interrupted by signal. Shutting down.\n";
  } else if (!g_sender_connected.load()) {
    std::cerr << "[Receiver] Timed out after 2 minutes with no sender connection. "
              << "Exiting as failure.\n";
    room->setDelegate(nullptr);
    room.reset();
    livekit::shutdown();
    return 1;
  } else {
    std::cout << "[Receiver] Session complete.\n";
  }

  room->setDelegate(nullptr);
  room.reset();
  livekit::shutdown();
  return 0;
}
