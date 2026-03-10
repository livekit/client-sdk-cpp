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
 * Caller for the rpc example.
 *
 * Connects to a LiveKit room as "caller" and sends a string to the
 * receiver's custom "print" RPC method every second. The receiver
 * sleeps for 1s, 10s, or 20s depending on the call number, so some
 * calls will take noticeably longer to return.
 *
 * Usage:
 *   RpcCaller <ws-url> <token>
 *   LIVEKIT_URL=... LIVEKIT_TOKEN=... RpcCaller
 *
 * Generate a token with:
 *   lk token create --join --room <room> --identity caller --valid-for 24h
 */

#include "livekit/rpc_error.h"
#include "session_manager/session_manager.h"

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
    std::cerr << "Usage: RpcCaller <ws-url> <token>\n"
              << "   or: LIVEKIT_URL=... LIVEKIT_TOKEN=... RpcCaller\n";
    return 1;
  }

  std::signal(SIGINT, handleSignal);

  session_manager::SessionManager sm;
  std::cout << "[caller] Connecting to " << url << " ...\n";

  livekit::RoomOptions options;
  if (!sm.connect(url, token, options)) {
    std::cerr << "[caller] Failed to connect.\n";
    return 1;
  }
  std::cout << "[caller] Connected.\n";

  // Give the receiver a moment to join and register its handler.
  for (int i = 0; i < 30 && g_running.load(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

  int count = 0;
  while (g_running.load()) {
    ++count;
    std::string message = "Hello from caller #" + std::to_string(count);

    std::cout << "[caller] #" << count << " Sending: \"" << message
              << "\" ...\n";

    auto t0 = std::chrono::steady_clock::now();
    try {
      auto response = sm.performRpc("receiver", "print", message, std::nullopt);
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - t0)
                         .count();
      if (response.has_value()) {
        std::cout << "[caller] #" << count << " Response: \""
                  << response.value() << "\" (" << elapsed << "ms)\n";
      } else {
        std::cout << "[caller] #" << count << " No response (" << elapsed
                  << "ms)\n";
      }
    } catch (const livekit::RpcError &e) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - t0)
                         .count();
      std::cerr << "[caller] #" << count << " RPC error (code=" << e.code()
                << " msg=\"" << e.message() << "\") (" << elapsed << "ms)\n";
    } catch (const std::exception &e) {
      std::cerr << "[caller] #" << count << " Error: " << e.what() << "\n";
    }

    for (int i = 0; i < 10 && g_running.load(); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "[caller] Shutting down...\n";
  sm.disconnect();
  std::cout << "[caller] Done.\n";
  return 0;
}
