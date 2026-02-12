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
#include "utils.h"
#include "livekit/livekit.h"

using namespace livekit;
using namespace std::chrono_literals;

namespace {

std::atomic<bool> g_running{true};
std::atomic<bool> g_human_connected{false};

void handleSignal(int) { g_running.store(false); }

void printUsage(const char *prog) {
  std::cerr << "Usage:\n"
            << "  " << prog << " <ws-url> <token>\n"
            << "or:\n"
            << "  " << prog << " --url=<ws-url> --token=<token>\n\n"
            << "Env fallbacks:\n"
            << "  LIVEKIT_URL, LIVEKIT_TOKEN\n\n"
            << "This is the 'robot' role. It waits for a 'human' peer to\n"
            << "connect and send joystick commands via RPC.\n"
            << "Exits after 2 minutes if no commands are received.\n";
}

} // namespace

int main(int argc, char *argv[]) {
  std::string url, token;
  if (!simple_robot::parseArgs(argc, argv, url, token)) {
    printUsage(argv[0]);
    return 1;
  }

  std::cout << "[Robot] Connecting to: " << url << "\n";
  std::signal(SIGINT, handleSignal);

  livekit::initialize(livekit::LogSink::kConsole);
  auto room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;

  bool res = room->Connect(url, token, options);
  std::cout << "[Robot] Connect result: " << std::boolalpha << res << "\n";
  if (!res) {
    std::cerr << "[Robot] Failed to connect to room\n";
    livekit::shutdown();
    return 1;
  }

  auto info = room->room_info();
  std::cout << "[Robot] Connected to room: " << info.name << "\n";
  std::cout << "[Robot] Waiting for 'human' peer (up to 2 minutes)...\n";

  // Register RPC handler for joystick commands
  LocalParticipant *lp = room->localParticipant();
  lp->registerRpcMethod(
      "joystick_command",
      [](const RpcInvocationData &data) -> std::optional<std::string> {
        try {
          auto cmd = simple_robot::json_to_joystick(data.payload);
          g_human_connected.store(true);
          std::cout << "[Robot] Joystick from '" << data.caller_identity
                    << "': x=" << cmd.x << " y=" << cmd.y << " z=" << cmd.z
                    << "\n";
          return std::optional<std::string>{"ok"};
        } catch (const std::exception &e) {
          std::cerr << "[Robot] Bad joystick payload: " << e.what() << "\n";
          throw;
        }
      });

  std::cout << "[Robot] RPC handler 'joystick_command' registered. "
            << "Listening for commands...\n";

  // Wait up to 2 minutes for activity, then exit as failure
  auto deadline = std::chrono::steady_clock::now() + 2min;

  while (g_running.load() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(100ms);
  }

  if (!g_running.load()) {
    std::cout << "[Robot] Interrupted by signal. Shutting down.\n";
  } else if (!g_human_connected.load()) {
    std::cerr << "[Robot] Timed out after 2 minutes with no human connection. "
              << "Exiting as failure.\n";
    room->setDelegate(nullptr);
    room.reset();
    livekit::shutdown();
    return 1;
  } else {
    std::cout << "[Robot] Session complete.\n";
  }

  room->setDelegate(nullptr);
  room.reset();
  livekit::shutdown();
  return 0;
}
