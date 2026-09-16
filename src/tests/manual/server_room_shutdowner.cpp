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

#include <livekit/livekit.h>
#include <livekit/room_event_types.h>
#include <livekit/token_source.h>
#include <signal.h>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>

class DummyDelegate : public livekit::RoomDelegate {
public:
  void onDisconnected(livekit::Room& room, const livekit::DisconnectedEvent& event) override {
    std::cout << "Room disconnected: " << room.roomInfo().name << "\n";
    disconnected = true;
    disconnect_cv.notify_all();
  }

  std::condition_variable disconnect_cv;
  std::mutex disconnect_mutex;
  bool disconnected = false;
};

std::optional<std::string> getEnv(const char* env) {
  const char* result = std::getenv(env);
  if (result != nullptr && result[0] != '\0') return result;
  return std::nullopt;
}

struct ConnectCredentials {
  std::string server_url;
  std::string participant_token;
};

std::optional<ConnectCredentials> loadConnectCredentials() {
  const auto token_server_id = getEnv("LIVEKIT_TOKEN_SERVER_ID");
  const auto url = getEnv("LIVEKIT_URL");
  auto token = getEnv("LIVEKIT_TOKEN");
  if (!token.has_value()) {
    token = getEnv("LIVEKIT_TOKEN_A");
  }

  const bool has_token_server = token_server_id.has_value();
  const bool has_url = url.has_value();
  const bool has_token = token.has_value();
  const bool has_direct = has_url || has_token;

  if (has_token_server && has_direct) {
    std::cerr << "Provide either LIVEKIT_TOKEN_SERVER_ID or LIVEKIT_URL + "
                 "LIVEKIT_TOKEN (or LIVEKIT_TOKEN_A), not both\n";
    return std::nullopt;
  }

  if (has_token_server) {
    auto token_source = livekit::DevelopmentTokenSource::create(token_server_id.value());
    if (!token_source) {
      std::cerr << "Failed to create token source\n";
      return std::nullopt;
    }

    livekit::TokenRequestOptions token_request_options;
    auto token_response = token_source->fetch(token_request_options).get();
    if (!token_response.ok()) {
      std::cerr << "Failed to fetch token from development token server\n";
      return std::nullopt;
    }

    return ConnectCredentials{token_response.value().server_url, token_response.value().participant_token};
  }

  if (has_url && has_token) {
    return ConnectCredentials{url.value(), token.value()};
  }

  if (has_url != has_token) {
    std::cerr << "LIVEKIT_URL and LIVEKIT_TOKEN (or LIVEKIT_TOKEN_A) must both be set\n";
    return std::nullopt;
  }

  std::cerr << "Set LIVEKIT_TOKEN_SERVER_ID, or LIVEKIT_URL and LIVEKIT_TOKEN "
               "(or LIVEKIT_TOKEN_A)\n";
  return std::nullopt;
}

std::atomic<bool> shutdown_requested = false;

void signalHandler(int signal) {
  std::cout << "Signal " << signal << " received\n";
  shutdown_requested = true;
}

int main(int argc, char** argv) {
  if (!livekit::initialize(livekit::LogLevel::Info)) {
    std::cerr << "Failed to initialize LiveKit\n";
    return 1;
  }

  const size_t num_iterations = argc > 1 ? std::stoi(argv[1]) : 1;
  std::cout << "Running " << num_iterations << " iterations\n";

  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  const auto credentials = loadConnectCredentials();
  if (!credentials.has_value()) {
    livekit::shutdown();
    return 1;
  }

  for (size_t i = 0; i < num_iterations && !shutdown_requested; i++) {
    std::cout << "-----Starting iteration " << i << "-----\n";

    livekit::Room room;
    DummyDelegate delegate; // Fresh delegate is needed so that boolean resets
    room.setDelegate(&delegate);
    if (!room.connect(credentials->server_url, credentials->participant_token, livekit::RoomOptions())) {
      std::cerr << "Failed to connect to room\n";
      livekit::shutdown();
      return 1;
    }

    std::cout << "Connected to room: " << room.roomInfo().name << "\n";

    std::cout << "Waiting for room to disconnect...\n";
    std::unique_lock<std::mutex> lock(delegate.disconnect_mutex);
    delegate.disconnect_cv.wait(lock, [&]() { return delegate.disconnected || shutdown_requested; });

    if (shutdown_requested) {
      std::cout << "Shutdown requested, stopping iteration\n";
      break;
    }

    std::cout << "Disconnected from room\n";
    std::cout << "-----Stopped iteration " << i << "-----\n";
  }

  std::cout << "Shutting down...\n";
  livekit::shutdown();

  return 0;
}