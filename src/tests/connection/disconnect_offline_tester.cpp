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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "tcp_fault_proxy.h"

namespace {

using namespace std::chrono_literals;

struct ServerAddress {
  std::string host;
  std::uint16_t port;
  std::string path;
};

struct Options {
  enum class DisconnectTiming {
    Immediate,
    AfterReconnecting,
  };

  enum class Operation {
    Disconnect,
    UnpublishTrack,
  };

  std::string url;
  std::string token;
  std::chrono::milliseconds offline_duration{120s};
  Operation operation{Operation::Disconnect};
  DisconnectTiming disconnect_timing{DisconnectTiming::Immediate};
};

/// Tracks the reconnect transition without blocking the FFI callback thread.
class ReconnectTrackingDelegate final : public livekit::RoomDelegate {
public:
  void onConnectionStateChanged(livekit::Room&, const livekit::ConnectionStateChangedEvent& event) override {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      connected_ = event.state == livekit::ConnectionState::Connected;
    }
    connected_cv_.notify_all();
  }

  void onReconnecting(livekit::Room&, const livekit::ReconnectingEvent&) override {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      reconnecting_ = true;
    }
    reconnecting_cv_.notify_all();
    std::cout << "ReconnectTrackingDelegate::onReconnecting invoked.\n";
  }

  void onDisconnected(livekit::Room&, const livekit::DisconnectedEvent&) override {
    std::cout << "ReconnectTrackingDelegate::onDisconnected invoked.\n";
  }

  bool waitForConnected(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return connected_cv_.wait_for(lock, timeout, [this]() { return connected_; });
  }

  bool waitForReconnecting(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return reconnecting_cv_.wait_for(lock, timeout, [this]() { return reconnecting_; });
  }

private:
  std::mutex mutex_;
  std::condition_variable reconnecting_cv_;
  std::condition_variable connected_cv_;
  bool reconnecting_{false};
  bool connected_{false};
};

[[noreturn]] void usage(const char* executable, const std::string& error = {}) {
  if (!error.empty()) std::cerr << "error: " << error << '\n';
  std::cerr << "Usage: " << executable
            << " [--url ws://host:port[/path]] [--token JWT] [--offline-duration-ms milliseconds]"
               " [--operation disconnect|unpublish-track]"
               " [--disconnect-timing immediate|after-reconnecting]\n"
               "Defaults: LIVEKIT_URL and LIVEKIT_TOKEN_A. This TCP proxy supports ws:// only.\n";
  std::exit(error.empty() ? EXIT_SUCCESS : EXIT_FAILURE);
}

std::string valueFromEnv(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? "" : value;
}

void setRustLogLevel() {
#if defined(_WIN32)
  if (_putenv_s("RUST_LOG", "info") != 0) throw std::runtime_error("Unable to set RUST_LOG");
#else
  if (setenv("RUST_LOG", "info", 1) != 0) throw std::runtime_error("Unable to set RUST_LOG");
#endif
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
    } else if (argument == "--disconnect-timing") {
      if (value == "immediate")
        options.disconnect_timing = Options::DisconnectTiming::Immediate;
      else if (value == "after-reconnecting")
        options.disconnect_timing = Options::DisconnectTiming::AfterReconnecting;
      else
        usage(argv[0], "invalid --disconnect-timing value");
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

} // namespace

int main(int argc, char* argv[]) {
  std::atomic_bool capture_running{true};
  try {
    std::cout << std::unitbuf;
    const Options options = parseOptions(argc, argv);
    const ServerAddress upstream = parseWsUrl(options.url);
    livekit::test::TcpFaultProxy proxy(upstream.host, upstream.port);
    proxy.start();
    const std::string proxy_url = "ws://127.0.0.1:" + std::to_string(proxy.listenPort()) + upstream.path;

    setRustLogLevel();

    std::cout << "Connecting through " << proxy_url << " to " << options.url << '\n';
    livekit::initialize(livekit::LogLevel::Debug);
    std::unique_ptr<livekit::Room> room = std::make_unique<livekit::Room>();
    auto delegate = std::make_unique<ReconnectTrackingDelegate>();
    room->setDelegate(delegate.get());
    if (!room->connect(proxy_url, options.token, {})) {
      livekit::shutdown();
      throw std::runtime_error("Room::connect failed");
    }

    // Give some time for the connection to be established
    if (!delegate->waitForConnected(10s)) {
      throw std::runtime_error("Connection timed out");
    }

    auto local_participant = room->localParticipant().lock();
    if (!local_participant) {
      throw std::runtime_error("Local participant invalid");
    }
    constexpr int kAudioSampleRate = 48000;
    constexpr int kAudioChannels = 1;
    constexpr int kAudioSamplesPerFrame = kAudioSampleRate / 100;
    constexpr int kVideoWidth = 320;
    constexpr int kVideoHeight = 180;
    constexpr auto kPublishDuration = 10s;

    auto audio_source = std::make_shared<livekit::AudioSource>(kAudioSampleRate, kAudioChannels);
    auto audio_track = local_participant->publishAudioTrack("offline-test-audio", audio_source,
                                                            livekit::TrackSource::SOURCE_MICROPHONE);
    if (!audio_track || !audio_track->publication()) {
      throw std::runtime_error("Publish audio track failed");
    }

    auto video_source = std::make_shared<livekit::VideoSource>(kVideoWidth, kVideoHeight);
    auto video_track =
        local_participant->publishVideoTrack("offline-test-video", video_source, livekit::TrackSource::SOURCE_CAMERA);
    if (!video_track || !video_track->publication()) {
      throw std::runtime_error("Publish video track failed");
    }
    const std::string video_track_sid = video_track->publication()->sid();

    std::cout << "Publishing audio and video for " << kPublishDuration.count()
              << " seconds before simulating the network loss.\n";
    std::thread audio_thread([audio_source, &capture_running]() {
      const livekit::AudioFrame frame =
          livekit::AudioFrame::create(kAudioSampleRate, kAudioChannels, kAudioSamplesPerFrame);
      auto next_frame = std::chrono::steady_clock::now();
      while (capture_running.load()) {
        try {
          audio_source->captureFrame(frame);
        } catch (const std::exception& error) {
          std::cerr << "Audio capture failed during disconnect: " << error.what() << '\n';
        }
        next_frame += 10ms;
        std::this_thread::sleep_until(next_frame);
      }
    });

    std::thread video_thread([video_source, &capture_running]() {
      livekit::VideoFrame frame =
          livekit::VideoFrame::create(kVideoWidth, kVideoHeight, livekit::VideoBufferType::RGBA);
      std::fill_n(frame.data(), frame.dataSize(), static_cast<std::uint8_t>(0x80));
      auto next_frame = std::chrono::steady_clock::now();
      while (capture_running.load()) {
        try {
          video_source->captureFrame(frame);
        } catch (const std::exception& error) {
          std::cerr << "Video capture failed during disconnect: " << error.what() << '\n';
        }
        next_frame += 33ms;
        std::this_thread::sleep_until(next_frame);
      }
    });

    std::this_thread::sleep_for(kPublishDuration);
    capture_running.store(false);
    audio_thread.join();
    video_thread.join();
    std::cout << "Finished the 10-second connected media period.\n";

    std::cout << "### Pausing proxy\n";
    proxy.pause();
    std::thread proxy_thread([&proxy, duration = options.offline_duration]() {
      std::this_thread::sleep_for(duration);
      std::cout << "### Resuming proxy\n";
      proxy.resume();
    });

    if (options.disconnect_timing == Options::DisconnectTiming::AfterReconnecting) {
      std::cout << "### Waiting for reconnect signal...\n";
      if (!delegate->waitForReconnecting(60s)) {
        proxy.resume();
        proxy_thread.join();
        capture_running.store(false);
        audio_thread.join();
        video_thread.join();
        throw std::runtime_error("Room did not enter Reconnecting within 60 seconds");
      }
    } else {
      std::cout << "### Disconnecting immediately, before LiveKit reports Reconnecting\n";
    }

    if (options.operation == Options::Operation::Disconnect) {
      // Match the corrected reporter sequence: application media sources are
      // released before an explicit client-initiated room disconnect.
      audio_source.reset();
      video_source.reset();
      std::cout << "### Calling Room::disconnect(ClientInitiated)\n";
      (void)room->disconnect(livekit::DisconnectReason::ClientInitiated);
    } else {
      std::cout << "### Calling LocalParticipant::unpublishTrack\n";
      local_participant->unpublishTrack(video_track_sid);
      audio_source.reset();
      video_source.reset();
    }

    proxy_thread.join();
    room.reset();
    std::cout << "### Resetting delegate\n";
    delegate.reset();
    std::cout << "### Shutting down LiveKit\n";
    livekit::shutdown();

    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "disconnect_offline_tester failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
