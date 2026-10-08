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
#include <livekit/local_data_track.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace std::chrono_literals;

constexpr std::string_view kLocalTestLiveKitUrl = "ws://localhost:7880";
constexpr int kFramesPerCycle = 3;

enum class Scenario {
  AudioTrack,
  VideoTrack,
  DataTrack,
  RoomReuse,
  RoomClientLeave,
  RoomServerDelete,
};

struct Options {
  Scenario scenario{Scenario::AudioTrack};
  std::uint64_t iterations{100};
  std::uint64_t warmup{20};
  std::uint64_t max_rss_growth_kib{std::uint64_t{8} * 1024U};
  std::uint64_t max_thread_growth{0};
};

struct ProcessSample {
  std::uint64_t rss_kib{};
  std::uint64_t threads{};
};

struct RoomCredentials {
  std::string url;
  std::string token;
};

class SdkLifetime {
public:
  SdkLifetime() {
    if (!livekit::initialize(livekit::LogLevel::Warn)) {
      throw std::runtime_error("LiveKit initialization failed");
    }
  }

  ~SdkLifetime() { livekit::shutdown(); }

  SdkLifetime(const SdkLifetime&) = delete;
  SdkLifetime& operator=(const SdkLifetime&) = delete;
};

class DisconnectTrackingDelegate final : public livekit::RoomDelegate {
public:
  void onDisconnected(livekit::Room&, const livekit::DisconnectedEvent& event) override {
    {
      const std::scoped_lock lock(mutex_);
      reason_ = event.reason;
      ++disconnect_count_;
    }
    condition_.notify_all();
  }

  void onRoomEos(livekit::Room&, const livekit::RoomEosEvent&) override {
    {
      const std::scoped_lock lock(mutex_);
      ++eos_count_;
    }
    condition_.notify_all();
  }

  bool waitForTeardown(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return condition_.wait_for(lock, timeout, [this]() { return disconnect_count_ > 0 && eos_count_ > 0; });
  }

  void requireDisconnect(livekit::DisconnectReason expected_reason) const {
    const std::scoped_lock lock(mutex_);
    if (disconnect_count_ != 1 || reason_ != expected_reason) {
      throw std::runtime_error("unexpected room disconnect event");
    }
  }

  void requireServerTeardown() const {
    const std::scoped_lock lock(mutex_);
    if (disconnect_count_ != 1 || eos_count_ != 1 || reason_ != livekit::DisconnectReason::RoomDeleted) {
      throw std::runtime_error("unexpected server room teardown events");
    }
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  livekit::DisconnectReason reason_{livekit::DisconnectReason::Unknown};
  int disconnect_count_{0};
  int eos_count_{0};
};

std::string_view scenarioName(Scenario scenario) {
  switch (scenario) {
    case Scenario::AudioTrack:
      return "audio-track";
    case Scenario::VideoTrack:
      return "video-track";
    case Scenario::DataTrack:
      return "data-track";
    case Scenario::RoomReuse:
      return "room-reuse";
    case Scenario::RoomClientLeave:
      return "room-client-leave";
    case Scenario::RoomServerDelete:
      return "room-server-delete";
  }
  throw std::runtime_error("unknown scenario");
}

Scenario parseScenario(std::string_view value) {
  if (value == "audio-track") {
    return Scenario::AudioTrack;
  }
  if (value == "video-track") {
    return Scenario::VideoTrack;
  }
  if (value == "data-track") {
    return Scenario::DataTrack;
  }
  if (value == "room-reuse") {
    return Scenario::RoomReuse;
  }
  if (value == "room-client-leave") {
    return Scenario::RoomClientLeave;
  }
  if (value == "room-server-delete") {
    return Scenario::RoomServerDelete;
  }
  throw std::runtime_error("invalid value for --scenario");
}

std::uint64_t parseUnsigned(std::string_view value, std::string_view option) {
  std::uint64_t parsed = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
    throw std::runtime_error("invalid value for " + std::string(option));
  }
  return parsed;
}

Options parseOptions(int argc, char* argv[]) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view option(argv[index]);
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value for " + std::string(option));
    }
    const std::string_view value(argv[++index]);
    if (option == "--scenario") {
      options.scenario = parseScenario(value);
    } else if (option == "--iterations") {
      options.iterations = parseUnsigned(value, option);
    } else if (option == "--warmup") {
      options.warmup = parseUnsigned(value, option);
    } else if (option == "--max-rss-growth-kib") {
      options.max_rss_growth_kib = parseUnsigned(value, option);
    } else if (option == "--max-thread-growth") {
      options.max_thread_growth = parseUnsigned(value, option);
    } else {
      throw std::runtime_error("unknown option: " + std::string(option));
    }
  }
  if (options.warmup == 0 || options.warmup >= options.iterations) {
    throw std::runtime_error("--warmup must be between 1 and --iterations - 1");
  }
  return options;
}

std::uint64_t statusValue(std::string_view line) {
  const auto separator = line.find(':');
  if (separator == std::string_view::npos) {
    throw std::runtime_error("malformed /proc/self/status value");
  }
  line.remove_prefix(separator + 1);
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())) != 0) {
    line.remove_prefix(1);
  }
  const auto end = line.find_first_not_of("0123456789");
  return parseUnsigned(line.substr(0, end), "/proc/self/status");
}

ProcessSample sampleProcess() {
  ProcessSample sample;
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.rfind("VmRSS:", 0) == 0) {
      sample.rss_kib = statusValue(line);
    } else if (line.rfind("Threads:", 0) == 0) {
      sample.threads = statusValue(line);
    }
  }
  if (sample.rss_kib == 0 || sample.threads == 0) {
    throw std::runtime_error("failed to read RSS or thread count from /proc/self/status");
  }
  return sample;
}

RoomCredentials roomCredentials() {
  const char* url = std::getenv("LIVEKIT_URL");
  const char* token = std::getenv("LIVEKIT_TOKEN_A");
  if (url == nullptr || token == nullptr || *url == '\0' || *token == '\0') {
    throw std::runtime_error("memory scenarios require LIVEKIT_URL and LIVEKIT_TOKEN_A");
  }
  return {url, token};
}

void connectRoom(livekit::Room& room, const RoomCredentials& credentials) {
  if (!room.connect(credentials.url, credentials.token, livekit::RoomOptions{})) {
    throw std::runtime_error("room connection failed");
  }
  if (room.connectionState() != livekit::ConnectionState::Connected || room.localParticipant().expired()) {
    throw std::runtime_error("room did not reach a connected state");
  }
}

// Exercises SDK startup, room connection, audio publication and capture,
// explicit unpublication, client disconnect, and complete process-side teardown.
void runAudioTrackCycle(const RoomCredentials& credentials) {
  SdkLifetime sdk;
  DisconnectTrackingDelegate delegate;
  livekit::Room room;
  room.setDelegate(&delegate);
  connectRoom(room, credentials);

  auto audio_source = std::make_shared<livekit::AudioSource>(48'000, 1);
  auto audio_track = livekit::LocalAudioTrack::createLocalAudioTrack("memory-audio", audio_source);
  if (!audio_track) {
    throw std::runtime_error("failed to create local audio track");
  }

  auto participant = room.localParticipant().lock();
  if (!participant) {
    throw std::runtime_error("local participant expired before publishing audio track");
  }
  livekit::TrackPublishOptions publish_options;
  publish_options.source = livekit::TrackSource::SOURCE_MICROPHONE;
  participant->publishTrack(audio_track, publish_options);
  const auto publication = audio_track->publication();
  if (!publication) {
    throw std::runtime_error("failed to publish local audio track");
  }

  auto frame = livekit::AudioFrame::create(48'000, 1, 480);
  std::fill(frame.data().begin(), frame.data().end(), 100);
  for (int frame_index = 0; frame_index < kFramesPerCycle; ++frame_index) {
    audio_source->captureFrame(frame, 1'000);
  }

  participant->unpublishTrack(publication->sid());
  if (!room.disconnect()) {
    throw std::runtime_error("client room disconnect failed");
  }
  delegate.requireDisconnect(livekit::DisconnectReason::ClientInitiated);
}

// Exercises SDK startup, room connection, video publication and capture,
// encoder/resource teardown, explicit unpublication, and client disconnect.
void runVideoTrackCycle(const RoomCredentials& credentials) {
  SdkLifetime sdk;
  DisconnectTrackingDelegate delegate;
  livekit::Room room;
  room.setDelegate(&delegate);
  connectRoom(room, credentials);

  auto video_source = std::make_shared<livekit::VideoSource>(640, 360);
  auto video_track = livekit::LocalVideoTrack::createLocalVideoTrack("memory-video", video_source);
  if (!video_track) {
    throw std::runtime_error("failed to create local video track");
  }

  auto participant = room.localParticipant().lock();
  if (!participant) {
    throw std::runtime_error("local participant expired before publishing video track");
  }
  livekit::TrackPublishOptions publish_options;
  publish_options.source = livekit::TrackSource::SOURCE_CAMERA;
  publish_options.simulcast = false;
  participant->publishTrack(video_track, publish_options);
  const auto publication = video_track->publication();
  if (!publication) {
    throw std::runtime_error("failed to publish local video track");
  }

  auto frame = livekit::VideoFrame::create(640, 360, livekit::VideoBufferType::I420);
  std::fill(frame.data(), frame.data() + frame.dataSize(), 0x7f);
  for (int frame_index = 0; frame_index < kFramesPerCycle; ++frame_index) {
    video_source->captureFrame(frame);
  }

  participant->unpublishTrack(publication->sid());
  if (!room.disconnect()) {
    throw std::runtime_error("client room disconnect failed");
  }
  delegate.requireDisconnect(livekit::DisconnectReason::ClientInitiated);
}

// Exercises data-track publication, payload enqueueing, explicit unpublication,
// and room teardown while the SDK remains initialized across cycles.
void runDataTrackCycle(const RoomCredentials& credentials) {
  DisconnectTrackingDelegate delegate;
  livekit::Room room;
  room.setDelegate(&delegate);
  connectRoom(room, credentials);

  auto participant = room.localParticipant().lock();
  if (!participant) {
    throw std::runtime_error("local participant expired before publishing data track");
  }
  auto publish_result = participant->publishDataTrack("memory-data");
  if (!publish_result) {
    throw std::runtime_error("failed to publish local data track");
  }

  const auto& track = publish_result.value();
  livekit::DataTrackFrame frame;
  frame.payload.assign(256, 0xFA);
  for (int frame_index = 0; frame_index < kFramesPerCycle; ++frame_index) {
    frame.user_timestamp = static_cast<std::uint64_t>(frame_index);
    if (!track->tryPush(frame)) {
      throw std::runtime_error("failed to push local data track frame");
    }
  }

  track->unpublishDataTrack();
  if (track->isPublished()) {
    throw std::runtime_error("local data track remained published");
  }
  if (!room.disconnect()) {
    throw std::runtime_error("client room disconnect failed");
  }
  delegate.requireDisconnect(livekit::DisconnectReason::ClientInitiated);
}

// Exercises repeated connect/disconnect cycles on one Room instance to detect
// connection state, participant, listener, or FFI resources retained by reuse.
void runRoomReuseCycle(livekit::Room& room, const RoomCredentials& credentials) {
  connectRoom(room, credentials);
  if (!room.disconnect()) {
    throw std::runtime_error("client room disconnect failed");
  }
  if (room.connectionState() != livekit::ConnectionState::Disconnected || !room.localParticipant().expired()) {
    throw std::runtime_error("reused room disconnect did not tear down local state");
  }
}

// Exercises application-initiated disconnect and destruction of a fresh Room,
// including local-participant cleanup and the expected disconnect callback.
void runRoomClientLeaveCycle(const RoomCredentials& credentials) {
  DisconnectTrackingDelegate delegate;
  livekit::Room room;
  room.setDelegate(&delegate);
  connectRoom(room, credentials);

  if (!room.disconnect()) {
    throw std::runtime_error("client room disconnect failed");
  }
  if (room.connectionState() != livekit::ConnectionState::Disconnected || !room.localParticipant().expired()) {
    throw std::runtime_error("client room disconnect did not tear down local state");
  }
  delegate.requireDisconnect(livekit::DisconnectReason::ClientInitiated);
}

// Exercises server-initiated room deletion and validates disconnect, event-stream
// completion, local-participant cleanup, and destruction of the torn-down Room.
void runRoomServerDeleteCycle(const RoomCredentials& credentials) {
  if (std::string_view(credentials.url) != kLocalTestLiveKitUrl) {
    throw std::runtime_error("room-server-delete requires LIVEKIT_URL=ws://localhost:7880");
  }

  DisconnectTrackingDelegate delegate;
  livekit::Room room;
  room.setDelegate(&delegate);
  connectRoom(room, credentials);

  // The room name is fixed rather than interpolating untrusted environment data into the command.
  if (std::system("lk --dev room delete --yes cpp_data_track_test") != 0) {
    throw std::runtime_error("failed to delete local test room");
  }
  if (!delegate.waitForTeardown(10s)) {
    throw std::runtime_error("timed out waiting for server room teardown");
  }
  if (room.connectionState() != livekit::ConnectionState::Disconnected || !room.localParticipant().expired()) {
    throw std::runtime_error("server room deletion did not tear down local state");
  }
  delegate.requireServerTeardown();
}

void runCycle(Scenario scenario, const RoomCredentials& credentials, livekit::Room* reusable_room) {
  switch (scenario) {
    case Scenario::AudioTrack:
      runAudioTrackCycle(credentials);
      return;
    case Scenario::VideoTrack:
      runVideoTrackCycle(credentials);
      return;
    case Scenario::DataTrack:
      runDataTrackCycle(credentials);
      return;
    case Scenario::RoomReuse:
      if (reusable_room == nullptr) {
        throw std::runtime_error("room-reuse scenario is missing its persistent room");
      }
      runRoomReuseCycle(*reusable_room, credentials);
      return;
    case Scenario::RoomClientLeave:
      runRoomClientLeaveCycle(credentials);
      return;
    case Scenario::RoomServerDelete:
      runRoomServerDeleteCycle(credentials);
      return;
  }
  throw std::runtime_error("unknown scenario");
}

} // namespace

int main(int argc, char* argv[]) {
  try {
    const Options options = parseOptions(argc, argv);
    const RoomCredentials credentials = roomCredentials();
    const bool per_cycle_sdk = options.scenario == Scenario::AudioTrack || options.scenario == Scenario::VideoTrack;
    std::unique_ptr<SdkLifetime> room_sdk;
    if (!per_cycle_sdk) {
      room_sdk = std::make_unique<SdkLifetime>();
    }
    std::unique_ptr<livekit::Room> reusable_room;
    if (options.scenario == Scenario::RoomReuse) {
      reusable_room = std::make_unique<livekit::Room>();
    }

    const ProcessSample initial = sampleProcess();
    ProcessSample warmup;
    ProcessSample final;
    for (std::uint64_t iteration = 1; iteration <= options.iterations; ++iteration) {
      runCycle(options.scenario, credentials, reusable_room.get());
      std::this_thread::sleep_for(100ms);
      if (iteration == options.warmup) {
        warmup = sampleProcess();
      } else if (iteration == options.iterations) {
        final = sampleProcess();
      }
    }

    const auto warmup_rss_growth =
        static_cast<std::int64_t>(warmup.rss_kib) - static_cast<std::int64_t>(initial.rss_kib);
    const auto final_rss_growth = static_cast<std::int64_t>(final.rss_kib) - static_cast<std::int64_t>(warmup.rss_kib);
    const auto warmup_thread_growth =
        static_cast<std::int64_t>(warmup.threads) - static_cast<std::int64_t>(initial.threads);
    const auto final_thread_growth =
        static_cast<std::int64_t>(final.threads) - static_cast<std::int64_t>(warmup.threads);
    const bool passed = final_rss_growth <= static_cast<std::int64_t>(options.max_rss_growth_kib) &&
                        final_thread_growth <= static_cast<std::int64_t>(options.max_thread_growth);
    std::cout << "memory lifecycle: scenario=" << scenarioName(options.scenario) << ", RSS 0 " << initial.rss_kib
              << " -> warmup " << warmup.rss_kib << " KiB (" << warmup_rss_growth << " KiB) -> final " << final.rss_kib
              << " KiB (" << final_rss_growth << " KiB), threads 0 " << initial.threads << " -> warmup "
              << warmup.threads << " (" << warmup_thread_growth << ") -> final " << final.threads << " ("
              << final_thread_growth << "), verdict=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "memory lifecycle tester failed: " << error.what() << '\n';
    return 1;
  }
}
