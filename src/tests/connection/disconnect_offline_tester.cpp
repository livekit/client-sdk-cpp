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

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include "tcp_fault_proxy.h"

namespace {

using namespace std::chrono_literals;

struct ServerAddress {
  std::string host;
  std::uint16_t port;
  std::string path;
};

struct Options {
  enum class Operation {
    Disconnect,
    UnpublishTrack,
  };

  std::string url;
  std::string token;
  std::chrono::milliseconds offline_duration{30s};
  Operation operation{Operation::Disconnect};
};

/// Mirrors the application-owned delegate in issue #222.
class ShutdownDelegate final : public livekit::RoomDelegate {
public:
  void onReconnecting(livekit::Room&, const livekit::ReconnectingEvent&) override {
    reconnecting_.store(true);
    std::cout << "ShutdownDelegate::onReconnecting invoked.\n";
  }

  void onDisconnected(livekit::Room&, const livekit::DisconnectedEvent&) override {
    std::cout << "ShutdownDelegate::onDisconnected invoked.\n";
  }

  bool reconnecting() const { return reconnecting_.load(); }

private:
  std::atomic_bool reconnecting_{false};
};

[[noreturn]] void usage(const char* executable, const std::string& error = {}) {
  if (!error.empty()) std::cerr << "error: " << error << '\n';
  std::cerr << "Usage: " << executable
            << " [--url ws://host:port[/path]] [--token JWT] [--offline-duration-ms milliseconds]"
               " [--operation disconnect|unpublish-track]\n"
               "Defaults: LIVEKIT_URL and LIVEKIT_TOKEN_A. This TCP proxy supports ws:// only.\n";
  std::exit(error.empty() ? EXIT_SUCCESS : EXIT_FAILURE);
}

std::string valueFromEnv(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? "" : value;
}

Options parseOptions(int argc, char* argv[]) {
  Options options{valueFromEnv("LIVEKIT_URL"), valueFromEnv("LIVEKIT_TOKEN_A")};
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help" || argument == "-h") usage(argv[0]);
    if (index + 1 == argc) usage(argv[0], "missing value for " + argument);
    const std::string value = argv[++index];
    if (argument == "--url")
      options.url = value;
    else if (argument == "--token")
      options.token = value;
    else if (argument == "--operation") {
      if (value == "disconnect")
        options.operation = Options::Operation::Disconnect;
      else if (value == "unpublish-track")
        options.operation = Options::Operation::UnpublishTrack;
      else
        usage(argv[0], "invalid --operation value");
    } else if (argument == "--offline-duration-ms") {
      try {
        options.offline_duration = std::chrono::milliseconds(std::stoll(value));
      } catch (const std::exception&) {
        usage(argv[0], "invalid --offline-duration-ms value");
      }
    } else {
      usage(argv[0], "unknown argument " + argument);
    }
  }
  if (options.url.empty()) usage(argv[0], "LIVEKIT_URL or --url is required");
  if (options.token.empty()) usage(argv[0], "LIVEKIT_TOKEN_A or --token is required");
  if (options.offline_duration.count() < 0) usage(argv[0], "offline duration must be non-negative");
  return options;
}

ServerAddress parseWsUrl(const std::string& url) {
  constexpr char kScheme[] = "ws://";
  if (url.compare(0, sizeof(kScheme) - 1, kScheme) != 0) {
    throw std::invalid_argument("the fault proxy requires a ws:// URL (wss:// is not supported)");
  }
  const std::string authority_and_path = url.substr(sizeof(kScheme) - 1);
  const auto path_start = authority_and_path.find('/');
  const std::string authority = authority_and_path.substr(0, path_start);
  if (authority.empty() || authority.find('@') != std::string::npos || authority.front() == '[') {
    throw std::invalid_argument("URL must use a hostname or IPv4 address with an explicit port");
  }
  const auto colon = authority.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 == authority.size()) {
    throw std::invalid_argument("URL must include an explicit port, for example ws://localhost:7880");
  }
  unsigned long port = 0;
  try {
    port = std::stoul(authority.substr(colon + 1));
  } catch (const std::exception&) {
    throw std::invalid_argument("URL contains an invalid port");
  }
  if (port == 0 || port > 65535) throw std::invalid_argument("URL port is outside 1..65535");
  return {authority.substr(0, colon), static_cast<std::uint16_t>(port),
          path_start == std::string::npos ? "" : authority_and_path.substr(path_start)};
}

bool waitForConnection(const livekit::test::TcpFaultProxy& proxy, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (proxy.acceptedConnectionCount() != 0) return true;
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

bool waitForReconnecting(const ShutdownDelegate& delegate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::cout << "### Waiting for reconnecting: " << delegate.reconnecting() << "\n";
    if (delegate.reconnecting()) return true;
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

} // namespace

int main(int argc, char* argv[]) {
  try {
    std::cout << std::unitbuf;
    const Options options = parseOptions(argc, argv);
    const ServerAddress upstream = parseWsUrl(options.url);
    livekit::test::TcpFaultProxy proxy(upstream.host, upstream.port);
    proxy.start();
    const std::string proxy_url = "ws://127.0.0.1:" + std::to_string(proxy.listenPort()) + upstream.path;

    std::cout << "Connecting through " << proxy_url << " to " << options.url << '\n';
    livekit::initialize(livekit::LogLevel::Debug);
    std::unique_ptr<livekit::Room> room = std::make_unique<livekit::Room>();
    auto delegate = std::make_unique<ShutdownDelegate>();
    room->setDelegate(delegate.get());
    if (!room->connect(proxy_url, options.token, {})) {
      livekit::shutdown();
      throw std::runtime_error("Room::connect failed");
    }
    if (!waitForConnection(proxy, 2s)) {
      (void)room->disconnect();
      livekit::shutdown();
      throw std::runtime_error("proxy did not observe the initial signal connection");
    }

    auto participant = room->localParticipant().lock();
    if (!participant) {
      (void)room->disconnect();
      livekit::shutdown();
      throw std::runtime_error("connected room has no local participant");
    }
    auto video_source = std::make_shared<livekit::VideoSource>(16, 16);
    auto video_track =
        participant->publishVideoTrack("offline-operation-track", video_source, livekit::TrackSource::SOURCE_CAMERA);
    if (!video_track || !video_track->publication()) {
      (void)room->disconnect();
      livekit::shutdown();
      throw std::runtime_error("failed to publish the local video track");
    }
    const std::string track_sid = video_track->publication()->sid();
    std::cout << "Published local video track " << track_sid << ".\n";

    std::cout << "Connected. Pausing proxy and resetting " << proxy.acceptedConnectionCount()
              << " established connection(s).\n";

    // Kick off thread in parallel to pause the proxy and reset the connections.
    std::thread pause_proxy([&proxy, duration = options.offline_duration]() {
      std::cout << "### Pausing connection\n";
      proxy.pause();
      proxy.resetConnections();

      // std::this_thread::sleep_for(duration);
      // std::cout << "### Restoring connection after " << duration.count() << " ms.\n";
      // proxy.resume();
    });

    if (!waitForReconnecting(*delegate, 5s)) {
      std::cout << "### Disconnecting room\n";
      (void)room->disconnect();
      livekit::shutdown();
      throw std::runtime_error("room did not enter Reconnecting after the signal connection was reset");
    }

    const auto started = std::chrono::steady_clock::now();
    std::optional<bool> disconnect_result;
    std::exception_ptr operation_error;
    try {
      if (options.operation == Options::Operation::Disconnect) {
        std::cout << "Calling Room::disconnect(ClientInitiated) with a live delegate; it should not wait for the "
                     "network.\n";
        disconnect_result = room->disconnect(livekit::DisconnectReason::ClientInitiated);
      } else {
        std::cout << "Calling LocalParticipant::unpublishTrack; it should not wait for the network.\n";
        participant->unpublishTrack(track_sid);
      }
    } catch (...) {
      operation_error = std::current_exception();
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    pause_proxy.join();
    if (operation_error) std::rethrow_exception(operation_error);

    if (disconnect_result.has_value()) {
      std::cout << "Room::disconnect returned " << std::boolalpha << *disconnect_result;
    } else {
      std::cout << "LocalParticipant::unpublishTrack returned";
      (void)room->disconnect();
    }
    std::cout << " after " << elapsed.count() << " ms.\n";

    room.reset();
    delegate.reset();
    livekit::shutdown();
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "disconnect_offline_tester failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
