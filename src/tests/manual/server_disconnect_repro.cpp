/*
 * Copyright 2026 LiveKit, Inc.
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

#include <livekit/data_track_frame.h>
#include <livekit/livekit.h>
#include <livekit/local_data_track.h>
#include <livekit/token_source.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

constexpr int kFrameWidth = 640;
constexpr int kFrameHeight = 360;
constexpr auto kFrameInterval = 100ms;
constexpr auto kDataInterval = 1s;
constexpr auto kPostRoomDestroyProbe = 5s;
constexpr const char* kPublisherIdentity = "cpp-server-disconnect-repro";
constexpr const char* kSubscriberIdentity = "cpp-server-disconnect-repro-subscriber";
constexpr const char* kVideoTrackName = "synthetic-camera";
constexpr const char* kDataTrackName = "shutdown-repro-data";

std::atomic<bool> g_exit_requested{false};
std::atomic<bool> g_repro_path_completed{false};

void handleSignal(int) { g_exit_requested.store(true, std::memory_order_relaxed); }

const char* disconnectReasonName(livekit::DisconnectReason reason) {
  using livekit::DisconnectReason;
  switch (reason) {
    case DisconnectReason::ClientInitiated:
      return "ClientInitiated";
    case DisconnectReason::DuplicateIdentity:
      return "DuplicateIdentity";
    case DisconnectReason::ServerShutdown:
      return "ServerShutdown";
    case DisconnectReason::ParticipantRemoved:
      return "ParticipantRemoved";
    case DisconnectReason::RoomDeleted:
      return "RoomDeleted";
    case DisconnectReason::StateMismatch:
      return "StateMismatch";
    case DisconnectReason::JoinFailure:
      return "JoinFailure";
    case DisconnectReason::Migration:
      return "Migration";
    case DisconnectReason::SignalClose:
      return "SignalClose";
    case DisconnectReason::RoomClosed:
      return "RoomClosed";
    case DisconnectReason::UserUnavailable:
      return "UserUnavailable";
    case DisconnectReason::UserRejected:
      return "UserRejected";
    case DisconnectReason::SipTrunkFailure:
      return "SipTrunkFailure";
    case DisconnectReason::ConnectionTimeout:
      return "ConnectionTimeout";
    case DisconnectReason::MediaFailure:
      return "MediaFailure";
    case DisconnectReason::Unknown:
      return "Unknown";
  }
  return "Unknown";
}

const char* connectionStateName(livekit::ConnectionState state) {
  using livekit::ConnectionState;
  switch (state) {
    case ConnectionState::Disconnected:
      return "Disconnected";
    case ConnectionState::Connected:
      return "Connected";
    case ConnectionState::Reconnecting:
      return "Reconnecting";
  }
  return "Unknown";
}

class ReproRoomDelegate final : public livekit::RoomDelegate {
public:
  explicit ReproRoomDelegate(std::string label) : label_(std::move(label)) {}

  void onConnectionStateChanged(livekit::Room&, const livekit::ConnectionStateChangedEvent& event) override {
    std::cout << "[" << label_ << " delegate] connection state changed: " << connectionStateName(event.state) << "\n";
  }

  void onDisconnected(livekit::Room&, const livekit::DisconnectedEvent& event) override {
    disconnected_.store(true, std::memory_order_relaxed);
    std::cout << "[" << label_ << " delegate] disconnected: reason=" << disconnectReasonName(event.reason) << " ("
              << static_cast<int>(event.reason) << ")\n";
    std::cout << "[" << label_ << " delegate] waiting for SIGINT; Room remains alive\n";
  }

  void onRoomEos(livekit::Room&, const livekit::RoomEosEvent&) override {
    std::cout << "[" << label_ << " delegate] room EOS received\n";
  }

  bool disconnected() const noexcept { return disconnected_.load(std::memory_order_relaxed); }

private:
  std::string label_;
  std::atomic<bool> disconnected_{false};
};

void fillFrame(livekit::VideoFrame& frame, std::uint32_t frame_index) {
  const auto blue = static_cast<std::uint8_t>((frame_index * 7U) % 255U);
  const auto green = static_cast<std::uint8_t>((frame_index * 13U) % 255U);
  const auto red = static_cast<std::uint8_t>((frame_index * 29U) % 255U);

  std::uint8_t* data = frame.data();
  for (std::size_t i = 0; i < frame.dataSize(); i += 4) {
    data[i] = blue;
    data[i + 1] = green;
    data[i + 2] = red;
    data[i + 3] = 255;
  }
}

void publishFrames(const std::shared_ptr<livekit::VideoSource>& video_source,
                   const std::shared_ptr<livekit::LocalDataTrack>& data_track, const std::atomic<bool>& publishing) {
  livekit::VideoFrame frame = livekit::VideoFrame::create(kFrameWidth, kFrameHeight, livekit::VideoBufferType::BGRA);
  const auto started_at = std::chrono::steady_clock::now();
  auto next_frame_at = started_at;
  auto next_data_at = started_at;
  std::uint32_t frame_index = 0;
  std::uint64_t data_index = 0;
  bool capture_error_logged = false;
  bool data_error_logged = false;

  while (publishing.load(std::memory_order_relaxed)) {
    fillFrame(frame, frame_index++);
    const auto now = std::chrono::steady_clock::now();
    const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now - started_at).count();
    try {
      video_source->captureFrame(frame, static_cast<std::int64_t>(timestamp));
    } catch (const std::exception& error) {
      if (!capture_error_logged) {
        std::cerr << "[publisher] video capture failed, continuing repro loop: " << error.what() << "\n";
        capture_error_logged = true;
      }
    }

    if (now >= next_data_at) {
      const std::string message = "server-disconnect-repro frame " + std::to_string(data_index++);
      std::vector<std::uint8_t> payload(message.begin(), message.end());
      const auto push_result = data_track->tryPush(std::move(payload));
      if (!push_result && !data_error_logged) {
        std::cerr << "[publisher] data push failed, continuing repro loop: " << push_result.error().message << "\n";
        data_error_logged = true;
      }
      next_data_at = now + kDataInterval;
    }

    next_frame_at += kFrameInterval;
    std::this_thread::sleep_until(next_frame_at);
  }

  std::cout << "[publisher] capture loop stopped\n";
}

int run() {
  const char* sandbox_id = std::getenv("LIVEKIT_SANDBOX_ID");
  if (sandbox_id == nullptr || std::string(sandbox_id).empty()) {
    std::cerr << "LIVEKIT_SANDBOX_ID must be set\n";
    return 1;
  }

  auto token_source = livekit::SandboxTokenSource::create(sandbox_id);
  livekit::TokenRequestOptions request_options;
  request_options.participant_identity = kPublisherIdentity;

  std::cout << "Fetching sandbox credentials...\n";
  auto credentials_result = token_source->fetch(request_options).get();
  if (!credentials_result) {
    std::cerr << "Failed to fetch sandbox credentials: " << credentials_result.error().message << "\n";
    return 1;
  }
  const auto credentials = std::move(credentials_result).value();

  ReproRoomDelegate publisher_delegate("publisher");
  auto room = std::make_unique<livekit::Room>();
  room->setDelegate(&publisher_delegate);

  livekit::RoomOptions room_options;
  room_options.auto_subscribe = true;
  room_options.dynacast = false;
  if (!room->connect(credentials.server_url, credentials.participant_token, room_options)) {
    std::cerr << "Failed to connect to the sandbox room\n";
    return 1;
  }

  auto local_participant = room->localParticipant().lock();
  if (local_participant == nullptr) {
    throw std::runtime_error("Local participant unavailable after connect");
  }

  std::cout << "Connected to room '" << room->roomInfo().name << "' as '" << local_participant->identity() << "'"
            << "\n";

  livekit::TokenRequestOptions subscriber_request_options;
  subscriber_request_options.room_name = room->roomInfo().name;
  subscriber_request_options.participant_identity = kSubscriberIdentity;

  std::cout << "Fetching sandbox credentials for subscriber...\n";
  auto subscriber_credentials_result = token_source->fetch(subscriber_request_options).get();
  if (!subscriber_credentials_result) {
    std::cerr << "Failed to fetch subscriber sandbox credentials: " << subscriber_credentials_result.error().message
              << "\n";
    return 1;
  }
  const auto subscriber_credentials = std::move(subscriber_credentials_result).value();

  ReproRoomDelegate subscriber_delegate("subscriber");
  auto subscriber_room = std::make_unique<livekit::Room>();
  subscriber_room->setDelegate(&subscriber_delegate);
  subscriber_room->setOnVideoFrameCallback(kPublisherIdentity, kVideoTrackName,
                                           [](const livekit::VideoFrame&, std::int64_t) {});
  const auto data_callback_id = subscriber_room->addOnDataFrameCallback(
      kPublisherIdentity, kDataTrackName, [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});

  if (!subscriber_room->connect(subscriber_credentials.server_url, subscriber_credentials.participant_token,
                                room_options)) {
    std::cerr << "Failed to connect subscriber to the sandbox room\n";
    return 1;
  }
  std::cout << "Connected subscriber to the same room; discard callbacks are registered (data callback id="
            << data_callback_id << ")\n";

  auto video_source = std::make_shared<livekit::VideoSource>(kFrameWidth, kFrameHeight);
  auto video_track = livekit::LocalVideoTrack::createLocalVideoTrack(kVideoTrackName, video_source);
  livekit::TrackPublishOptions video_options;
  video_options.source = livekit::TrackSource::SOURCE_CAMERA;
  video_options.simulcast = false;
  local_participant->publishTrack(video_track, video_options);
  std::cout << "Published synthetic camera track\n";

  auto data_track_result = local_participant->publishDataTrack(kDataTrackName);
  if (!data_track_result) {
    throw std::runtime_error("Failed to publish data track: " + data_track_result.error().message);
  }
  auto data_track = std::move(data_track_result).value();
  std::cout << "Published data track\n";
  std::cout << "Subscriber is receiving and discarding the published video/data frames\n";
  std::cout << "Delete the room remotely, wait for the disconnect callback, then press Ctrl-C\n";

  std::atomic<bool> publishing{true};
  std::thread publisher([&]() { publishFrames(video_source, data_track, publishing); });

  while (!g_exit_requested.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(100ms);
  }

  std::cout << "SIGINT received; destroying Rooms without an explicit disconnect while publisher is active...\n";
  local_participant.reset();
  room.reset();
  subscriber_room.reset();
  std::cout << "Rooms destroyed; probing for stale FFI callbacks for " << kPostRoomDestroyProbe.count() << "s\n";
  std::this_thread::sleep_for(kPostRoomDestroyProbe);

  publishing.store(false, std::memory_order_relaxed);
  if (publisher.joinable()) {
    publisher.join();
  }

  std::cout << "Releasing track resources\n";
  data_track.reset();
  video_track.reset();
  video_source.reset();
  g_repro_path_completed.store(true, std::memory_order_relaxed);
  std::cout << "Track resources released\n";
  return 0;
}

} // namespace

int main() {
  std::signal(SIGINT, handleSignal);
  livekit::initialize(livekit::LogLevel::Info);

  int exit_code = 0;
  try {
    exit_code = run();
  } catch (const std::exception& error) {
    std::cerr << "Fatal error: " << error.what() << "\n";
    exit_code = 1;
  }

  if (g_repro_path_completed.load(std::memory_order_relaxed)) {
    std::cout << "Skipping livekit::shutdown() to preserve the bad-shutdown reproduction path\n";
  } else {
    livekit::shutdown();
  }
  return exit_code;
}
