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
 * UserTimestampedVideoConsumer
 *
 * Receives remote camera frames via Room::setOnVideoFrameEventCallback() and
 * logs any VideoFrameMetadata::user_timestamp_us values that arrive.
 */

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "livekit/livekit.h"

using namespace livekit;

namespace {

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

std::string getenvOrEmpty(const char *name) {
  const char *value = std::getenv(name);
  return value ? std::string(value) : std::string{};
}

std::string
formatUserTimestamp(const std::optional<VideoFrameMetadata> &metadata) {
  if (!metadata || !metadata->user_timestamp_us.has_value()) {
    return "n/a";
  }

  return std::to_string(*metadata->user_timestamp_us);
}

void printUsage(const char *program) {
  std::cerr << "Usage:\n"
            << "  " << program << " <ws-url> <token> [--ignore-user-timestamp]\n"
            << "or:\n"
            << "  LIVEKIT_URL=... LIVEKIT_TOKEN=... " << program
            << " [--ignore-user-timestamp]\n";
}

bool parseArgs(int argc, char *argv[], std::string &url, std::string &token,
               bool &read_user_timestamp) {
  read_user_timestamp = true;
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      return false;
    }
    if (arg == "--ignore-user-timestamp") {
      read_user_timestamp = false;
      continue;
    }
    if (arg == "--read-user-timestamp") {
      read_user_timestamp = true;
      continue;
    }

    positional.push_back(arg);
  }

  url = getenvOrEmpty("LIVEKIT_URL");
  token = getenvOrEmpty("LIVEKIT_TOKEN");

  if (positional.size() >= 2) {
    url = positional[0];
    token = positional[1];
  }

  return !(url.empty() || token.empty());
}

class UserTimestampedVideoConsumerDelegate : public RoomDelegate {
public:
  UserTimestampedVideoConsumerDelegate(Room &room, bool read_user_timestamp)
      : room_(room), read_user_timestamp_(read_user_timestamp) {}

  void registerExistingParticipants() {
    for (const auto &participant : room_.remoteParticipants()) {
      if (participant) {
        registerRemoteCameraCallback(participant->identity());
      }
    }
  }

  void onParticipantConnected(Room &,
                              const ParticipantConnectedEvent &event) override {
    if (!event.participant) {
      return;
    }

    std::cout << "[consumer] participant connected: "
              << event.participant->identity() << "\n";
    registerRemoteCameraCallback(event.participant->identity());
  }

  void onParticipantDisconnected(
      Room &, const ParticipantDisconnectedEvent &event) override {
    if (!event.participant) {
      return;
    }

    const std::string identity = event.participant->identity();
    room_.clearOnVideoFrameCallback(identity, TrackSource::SOURCE_CAMERA);

    {
      std::lock_guard<std::mutex> lock(mutex_);
      registered_identities_.erase(identity);
    }

    std::cout << "[consumer] participant disconnected: " << identity << "\n";
  }

private:
  void registerRemoteCameraCallback(const std::string &identity) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!registered_identities_.insert(identity).second) {
        return;
      }
    }

    VideoStream::Options stream_options;
    stream_options.format = VideoBufferType::RGBA;

    if (read_user_timestamp_) {
      room_.setOnVideoFrameEventCallback(
          identity, TrackSource::SOURCE_CAMERA,
          [identity](const VideoFrameEvent &event) {
            std::cout << "[consumer] from=" << identity
                      << " size=" << event.frame.width() << "x"
                      << event.frame.height()
                      << " capture_ts_us=" << event.timestamp_us
                      << " user_ts_us=" << formatUserTimestamp(event.metadata)
                      << " rotation=" << static_cast<int>(event.rotation)
                      << "\n";
          },
          stream_options);
    } else {
      room_.setOnVideoFrameCallback(
          identity, TrackSource::SOURCE_CAMERA,
          [identity](const VideoFrame &frame, const std::int64_t timestamp_us) {
            std::cout << "[consumer] from=" << identity
                      << " size=" << frame.width() << "x" << frame.height()
                      << " capture_ts_us=" << timestamp_us
                      << " user_ts_us=ignored\n";
          },
          stream_options);
    }

    std::cout << "[consumer] listening for camera frames from " << identity
              << " with user timestamp "
              << (read_user_timestamp_ ? "enabled" : "ignored") << "\n";
  }

  Room &room_;
  bool read_user_timestamp_;
  std::mutex mutex_;
  std::unordered_set<std::string> registered_identities_;
};

} // namespace

int main(int argc, char *argv[]) {
  std::string url;
  std::string token;
  bool read_user_timestamp = true;

  if (!parseArgs(argc, argv, url, token, read_user_timestamp)) {
    printUsage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif

  livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);
  int exit_code = 0;

  {
    Room room;
    RoomOptions options;
    options.auto_subscribe = true;
    options.dynacast = false;

    UserTimestampedVideoConsumerDelegate delegate(room, read_user_timestamp);
    room.setDelegate(&delegate);

    std::cout << "[consumer] connecting to " << url << "\n";
    if (!room.Connect(url, token, options)) {
      std::cerr << "[consumer] failed to connect\n";
      exit_code = 1;
    } else {
      std::cout << "[consumer] connected as "
                << room.localParticipant()->identity() << " to room '"
                << room.room_info().name << "' with user timestamp "
                << (read_user_timestamp ? "enabled" : "ignored") << "\n";

      delegate.registerExistingParticipants();

      while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }

      for (const auto &participant : room.remoteParticipants()) {
        if (participant) {
          room.clearOnVideoFrameCallback(participant->identity(),
                                         TrackSource::SOURCE_CAMERA);
        }
      }
    }

    room.setDelegate(nullptr);
  }

  livekit::shutdown();
  return exit_code;
}
