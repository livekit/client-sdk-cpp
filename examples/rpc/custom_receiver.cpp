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

/*
 * Receiver for the rpc example.
 *
 * Connects to a LiveKit room as "receiver", registers a custom RPC method
 * called "print", and prints whatever string the caller sends.
 *
 * Usage:
 *   RpcReceiver <ws-url> <token>
 *   LIVEKIT_URL=... LIVEKIT_TOKEN=... RpcReceiver
 *
 * Generate a token with:
 *   lk token create --join --room <room> --identity receiver --valid-for 24h
 */

#include "livekit/session_manager/session_manager.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

static std::atomic<bool> g_running{true};
static void handleSignal(int) { g_running.store(false); }

int main(int argc, char *argv[]) {
  std::string url, token;
  if (argc >= 3) {
    url = argv[1];
    token = argv[2];
  } else {
    const char *e = std::getenv("LIVEKIT_URL");
    if (e)
      url = e;
    e = std::getenv("LIVEKIT_TOKEN");
    if (e)
      token = e;
  }
  if (url.empty() || token.empty()) {
    std::cerr << "Usage: RpcReceiver <ws-url> <token>\n"
              << "   or: LIVEKIT_URL=... LIVEKIT_TOKEN=... RpcReceiver\n";
    return 1;
  }

  std::signal(SIGINT, handleSignal);

  session_manager::SessionManager sm;
  std::cout << "[receiver] Connecting to " << url << " ...\n";

  livekit::RoomOptions options;
  if (!sm.connect(url, token, options)) {
    std::cerr << "[receiver] Failed to connect.\n";
    return 1;
  }
  std::cout << "[receiver] Connected.\n";

  std::atomic<int> call_count{0};

  sm.registerRpcMethod(
      "print",
      [&call_count](const livekit::RpcInvocationData &data)
          -> std::optional<std::string> {
        int n = call_count.fetch_add(1) + 1;

        int sleep_sec = 1;
        if (n % 10 == 0)
          sleep_sec = 20;
        else if (n % 5 == 0)
          sleep_sec = 10;

        std::cout << "[receiver] Call #" << n << " from "
                  << data.caller_identity << ": \"" << data.payload
                  << "\" (sleeping " << sleep_sec << "s)\n";

        std::this_thread::sleep_for(std::chrono::seconds(sleep_sec));

        std::cout << "[receiver] Call #" << n << " done.\n";
        return "ok (slept " + std::to_string(sleep_sec) + "s)";
      });

  std::cout << "[receiver] Registered RPC method \"print\".\n"
            << "[receiver]   call %10==0 -> 20s sleep\n"
            << "[receiver]   call %5==0  -> 10s sleep\n"
            << "[receiver]   otherwise   ->  1s sleep\n"
            << "[receiver] Waiting for calls...\n";

  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "[receiver] Shutting down...\n";
  sm.disconnect();
  std::cout << "[receiver] Done.\n";
  return 0;
}
