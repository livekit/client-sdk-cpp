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

#include <livekit/data_track_stream.h>
#include <livekit/local_data_track.h>
#include <livekit/remote_data_track.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../common/test_common.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr char kTrackNamePrefix[] = "data_track_stress";
constexpr auto kTrackWaitTimeout = 10s;

std::string makeTrackName(const std::string& suffix) {
  return std::string(kTrackNamePrefix) + "_" + suffix + "_" + std::to_string(getTimestampUs());
}

template <typename Error>
std::string describeDataTrackError(const Error& error) {
  return "code=" + std::to_string(static_cast<std::uint32_t>(error.code)) + " message=" + error.message;
}

std::shared_ptr<LocalDataTrack> requirePublishedTrack(LocalParticipant* participant, const std::string& name) {
  auto result = participant->publishDataTrack(name);
  if (!result) {
    throw std::runtime_error("Failed to publish data track: " + describeDataTrackError(result.error()));
  }
  return result.value();
}

void requirePushSuccess(const Result<void, LocalDataTrackTryPushError>& result, const std::string& context) {
  if (!result) {
    throw std::runtime_error(context + ": " + describeDataTrackError(result.error()));
  }
}

class DataTrackPublishedDelegate : public RoomDelegate {
public:
  void onDataTrackPublished(Room&, const DataTrackPublishedEvent& event) override {
    if (!event.track) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    tracks_.push_back(event.track);
    cv_.notify_all();
  }

  std::shared_ptr<RemoteDataTrack> waitForTrack(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] { return !tracks_.empty(); })) {
      return nullptr;
    }
    return tracks_.front();
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::shared_ptr<RemoteDataTrack>> tracks_;
};

void runDataTrackTransportStress(double publish_fps, std::size_t payload_len) {
  const auto track_name = makeTrackName("transport");

  constexpr auto PUBLISH_DURATION = 10s;
  constexpr float MIN_PERCENTAGE = 0.90f;

  std::vector<TestRoomConnectionOptions> room_configs(2);
  room_configs[0].room_options.single_peer_connection = false;
  room_configs[1].room_options.single_peer_connection = false;

  DataTrackPublishedDelegate subscriber_delegate;
  room_configs[1].delegate = &subscriber_delegate;

  auto rooms = testRooms(room_configs);
  auto& publisher_room = rooms[0];
  const auto publisher_identity = publisher_room->localParticipant()->identity();

  auto track = requirePublishedTrack(publisher_room->localParticipant(), track_name);
  std::cerr << "Track published\n";

  auto remote_track = subscriber_delegate.waitForTrack(kTrackWaitTimeout);
  ASSERT_NE(remote_track, nullptr) << "Timed out waiting for remote data track";
  std::cerr << "Got remote track: " << remote_track->info().sid << "\n";

  EXPECT_TRUE(remote_track->isPublished());
  EXPECT_FALSE(remote_track->info().uses_e2ee);
  EXPECT_EQ(remote_track->info().name, track_name);
  EXPECT_EQ(remote_track->publisherIdentity(), publisher_identity);

  const auto frame_count =
      static_cast<std::size_t>(std::llround(std::chrono::duration<double>(PUBLISH_DURATION).count() * publish_fps));

  auto publish = [&]() {
    if (!track->isPublished()) {
      throw std::runtime_error("Publisher failed to publish data track");
    }
    if (track->info().uses_e2ee) {
      throw std::runtime_error("Unexpected E2EE on test data track");
    }
    if (track->info().name != track_name) {
      throw std::runtime_error("Published track name mismatch");
    }

    const auto frame_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / publish_fps));
    auto next_send = std::chrono::steady_clock::now();

    std::cout << "Publishing " << frame_count << " frames with payload length " << payload_len << '\n';
    for (std::size_t index = 0; index < frame_count; ++index) {
      std::vector<std::uint8_t> payload(payload_len, static_cast<std::uint8_t>(index));
      requirePushSuccess(track->tryPush(std::move(payload)), "Failed to push data frame");

      next_send += frame_interval;
      std::this_thread::sleep_until(next_send);
    }

    track->unpublishDataTrack();
  };

  auto subscribe_result = remote_track->subscribe();
  if (!subscribe_result) {
    FAIL() << describeDataTrackError(subscribe_result.error());
  }
  auto subscription = subscribe_result.value();

  std::promise<std::size_t> receive_count_promise;
  auto receive_count_future = receive_count_promise.get_future();

  auto subscribe = [&]() {
    std::size_t received_count = 0;
    DataTrackFrame frame;
    while (subscription->read(frame) && received_count < frame_count) {
      if (frame.payload.empty()) {
        throw std::runtime_error("Received empty data frame");
      }

      const auto first_byte = frame.payload.front();
      if (!std::all_of(frame.payload.begin(), frame.payload.end(),
                       [first_byte](std::uint8_t byte) { return byte == first_byte; })) {
        throw std::runtime_error("Received frame with inconsistent payload");
      }
      if (frame.user_timestamp.has_value()) {
        throw std::runtime_error("Received unexpected user timestamp in transport test");
      }

      ++received_count;
    }

    receive_count_promise.set_value(received_count);
  };

  auto pub_fut = std::async(std::launch::async, publish);
  auto sub_fut = std::async(std::launch::async, subscribe);

  const auto deadline = std::chrono::steady_clock::now() + PUBLISH_DURATION + 25s;

  const bool pub_ok = pub_fut.wait_until(deadline) == std::future_status::ready;
  const bool sub_ok = sub_fut.wait_until(deadline) == std::future_status::ready;

  if (!pub_ok || !sub_ok) {
    ADD_FAILURE() << "Timed out waiting for data frames";
  }

  pub_fut.get();
  sub_fut.get();

  const auto received_count = receive_count_future.get();
  const auto received_percent = static_cast<float>(received_count) / static_cast<float>(frame_count);
  std::cout << "Received " << received_count << "/" << frame_count << " frames (" << received_percent * 100.0f << "%)"
            << '\n';

  EXPECT_GE(received_percent, MIN_PERCENTAGE) << "Received " << received_count << "/" << frame_count << " frames";
}

} // namespace

class DataTrackStressTest : public LiveKitTestBase {};

TEST_F(DataTrackStressTest, LowFpsMultiPacket) {
  runDataTrackTransportStress(10.0, std::size_t{196608});
}

TEST_F(DataTrackStressTest, HighFpsMultiPacket) {
  runDataTrackTransportStress(120.0, std::size_t{8192});
}

} // namespace livekit::test
