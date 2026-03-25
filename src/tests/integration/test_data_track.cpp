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

// This test is used to verify that data tracks are published and received
// correctly. It is the same implementation as the rust
// client-sdk-rust/livekit/tests/data_track_test.rs test. To run this test, run
// a local SFU, set credentials examples/tokens/set_test_tokens.bash, and run:
//   ./build-debug/bin/livekit_integration_tests

#include "../common/test_common.h"

#include <cmath>
#include <condition_variable>
#include <exception>
#include <future>
#include <livekit/data_track_subscription.h>
#include <livekit/e2ee.h>
#include <livekit/remote_data_track.h>
#include <tuple>

namespace livekit {
namespace test {

using namespace std::chrono_literals;

namespace {

constexpr char kTrackNamePrefix[] = "data_track_e2e";
constexpr auto kPublishDuration = 5s;
constexpr auto kTrackWaitTimeout = 10s;
constexpr auto kReadTimeout = 30s;
constexpr auto kPollingInterval = 10ms;
constexpr float kMinimumReceivedPercent = 0.95f;
constexpr int kResubscribeIterations = 10;
constexpr int kPublishManyTrackCount = 256;
constexpr auto kPublishManyTimeout = 5s;
constexpr std::size_t kLargeFramePayloadBytes = 196608;
constexpr char kE2EESharedSecret[] = "password";
constexpr int kE2EEFrameCount = 5;

std::string makeTrackName(const std::string &suffix) {
  return std::string(kTrackNamePrefix) + "_" + suffix + "_" +
         std::to_string(getTimestampUs());
}

std::vector<std::uint8_t> e2eeSharedKey() {
  return std::vector<std::uint8_t>(
      kE2EESharedSecret, kE2EESharedSecret + sizeof(kE2EESharedSecret) - 1);
}

E2EEOptions makeE2EEOptions() {
  E2EEOptions options;
  options.key_provider_options.shared_key = e2eeSharedKey();
  return options;
}

std::vector<TestRoomConnectionOptions>
encryptedRoomConfigs(RoomDelegate *subscriber_delegate) {
  std::vector<TestRoomConnectionOptions> room_configs(2);
  room_configs[0].room_options.encryption = makeE2EEOptions();
  room_configs[1].room_options.encryption = makeE2EEOptions();
  room_configs[1].delegate = subscriber_delegate;
  return room_configs;
}

template <typename Predicate>
bool waitForCondition(Predicate &&predicate, std::chrono::milliseconds timeout,
                      std::chrono::milliseconds interval = kPollingInterval) {
  auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < timeout) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(interval);
  }
  return false;
}

class DataTrackPublishedDelegate : public RoomDelegate {
public:
  void onDataTrackPublished(Room &,
                            const DataTrackPublishedEvent &event) override {
    if (!event.track) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    tracks_.push_back(event.track);
    cv_.notify_all();
  }

  std::shared_ptr<RemoteDataTrack>
  waitForTrack(std::chrono::milliseconds timeout) {
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

DataFrame
readFrameWithTimeout(const std::shared_ptr<DataTrackSubscription> &subscription,
                     std::chrono::milliseconds timeout) {
  std::promise<DataFrame> frame_promise;
  auto future = frame_promise.get_future();

  std::thread reader([subscription,
                      promise = std::move(frame_promise)]() mutable {
    try {
      DataFrame frame;
      if (!subscription->read(frame)) {
        throw std::runtime_error("Subscription ended before a frame arrived");
      }
      promise.set_value(std::move(frame));
    } catch (...) {
      promise.set_exception(std::current_exception());
    }
  });

  if (future.wait_for(timeout) != std::future_status::ready) {
    subscription->close();
  }

  reader.join();
  return future.get();
}

} // namespace

class DataTrackE2ETest : public LiveKitTestBase {};

class DataTrackTransportTest
    : public DataTrackE2ETest,
      public ::testing::WithParamInterface<std::tuple<double, size_t>> {};

TEST_P(DataTrackTransportTest, PublishesAndReceivesFramesEndToEnd) {
  const auto publish_fps = std::get<0>(GetParam());
  const auto payload_len = std::get<1>(GetParam());
  const auto track_name = makeTrackName("transport");
  const auto frame_count = static_cast<size_t>(std::llround(
      std::chrono::duration<double>(kPublishDuration).count() * publish_fps));

  DataTrackPublishedDelegate subscriber_delegate;
  std::vector<TestRoomConnectionOptions> room_configs(2);
  room_configs[1].delegate = &subscriber_delegate;

  auto rooms = testRooms(room_configs);
  auto &publisher_room = rooms[0];
  const auto publisher_identity =
      publisher_room->localParticipant()->identity();

  std::exception_ptr publish_error;
  std::thread publisher([&]() {
    try {
      auto track =
          publisher_room->localParticipant()->publishDataTrack(track_name);
      if (!track || !track->isPublished()) {
        throw std::runtime_error("Publisher failed to publish data track");
      }
      if (track->info().uses_e2ee) {
        throw std::runtime_error("Unexpected E2EE on test data track");
      }
      if (track->info().name != track_name) {
        throw std::runtime_error("Published track name mismatch");
      }

      const auto frame_interval =
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<double>(1.0 / publish_fps));
      auto next_send = std::chrono::steady_clock::now();

      std::cout << "Publishing " << frame_count
                << " frames with payload length " << payload_len << std::endl;
      for (size_t index = 0; index < frame_count; ++index) {
        std::vector<std::uint8_t> payload(payload_len,
                                          static_cast<std::uint8_t>(index));
        if (!track->tryPush(payload)) {
          throw std::runtime_error("Failed to push data frame");
        }

        next_send += frame_interval;
        std::this_thread::sleep_until(next_send);
      }

      track->unpublishDataTrack();
    } catch (...) {
      publish_error = std::current_exception();
    }
  });

  auto remote_track = subscriber_delegate.waitForTrack(kTrackWaitTimeout);
  ASSERT_NE(remote_track, nullptr) << "Timed out waiting for remote data track";
  EXPECT_TRUE(remote_track->isPublished());
  EXPECT_FALSE(remote_track->info().uses_e2ee);
  EXPECT_EQ(remote_track->info().name, track_name);
  EXPECT_EQ(remote_track->publisherIdentity(), publisher_identity);

  auto subscription = remote_track->subscribe();
  ASSERT_NE(subscription, nullptr);

  std::promise<size_t> receive_count_promise;
  auto receive_count_future = receive_count_promise.get_future();
  std::exception_ptr subscribe_error;
  std::thread subscriber([&]() {
    try {
      size_t received_count = 0;
      DataFrame frame;
      while (subscription->read(frame) && received_count < frame_count) {
        if (frame.payload.empty()) {
          throw std::runtime_error("Received empty data frame");
        }

        const auto first_byte = frame.payload.front();
        if (!std::all_of(frame.payload.begin(), frame.payload.end(),
                         [first_byte](std::uint8_t byte) {
                           return byte == first_byte;
                         })) {
          throw std::runtime_error("Received frame with inconsistent payload");
        }
        if (frame.user_timestamp.has_value()) {
          throw std::runtime_error(
              "Received unexpected user timestamp in transport test");
        }

        ++received_count;
      }

      receive_count_promise.set_value(received_count);
    } catch (...) {
      subscribe_error = std::current_exception();
      receive_count_promise.set_exception(std::current_exception());
    }
  });

  if (receive_count_future.wait_for(kReadTimeout) !=
      std::future_status::ready) {
    subscription->close();
    ADD_FAILURE() << "Timed out waiting for data frames";
  }

  subscriber.join();
  publisher.join();

  if (publish_error) {
    std::rethrow_exception(publish_error);
  }
  if (subscribe_error) {
    std::rethrow_exception(subscribe_error);
  }

  const auto received_count = receive_count_future.get();
  const auto received_percent =
      static_cast<float>(received_count) / static_cast<float>(frame_count);
  std::cout << "Received " << received_count << "/" << frame_count
            << " frames (" << received_percent * 100.0f << "%)" << std::endl;

  EXPECT_GE(received_percent, kMinimumReceivedPercent)
      << "Received " << received_count << "/" << frame_count << " frames";
}

TEST_F(DataTrackE2ETest, UnpublishUpdatesPublishedStateEndToEnd) {
  const auto track_name = makeTrackName("published_state");

  DataTrackPublishedDelegate subscriber_delegate;
  std::vector<TestRoomConnectionOptions> room_configs(2);
  room_configs[1].delegate = &subscriber_delegate;

  auto rooms = testRooms(room_configs);
  auto &publisher_room = rooms[0];

  auto local_track =
      publisher_room->localParticipant()->publishDataTrack(track_name);
  ASSERT_NE(local_track, nullptr);
  ASSERT_TRUE(local_track->isPublished());

  auto remote_track = subscriber_delegate.waitForTrack(kTrackWaitTimeout);
  ASSERT_NE(remote_track, nullptr) << "Timed out waiting for remote data track";
  EXPECT_TRUE(remote_track->isPublished());

  std::this_thread::sleep_for(500ms);
  local_track->unpublishDataTrack();

  EXPECT_FALSE(local_track->isPublished());
  EXPECT_TRUE(
      waitForCondition([&]() { return !remote_track->isPublished(); }, 2s))
      << "Remote track did not report unpublished state";
}

TEST_F(DataTrackE2ETest, PublishManyTracks) {
  auto rooms = testRooms(1);
  auto &room = rooms[0];

  std::vector<std::shared_ptr<LocalDataTrack>> tracks;
  tracks.reserve(kPublishManyTrackCount);

  const auto start = std::chrono::steady_clock::now();
  for (int index = 0; index < kPublishManyTrackCount; ++index) {
    const auto track_name = "track_" + std::to_string(index);
    auto track = room->localParticipant()->publishDataTrack(track_name);

    ASSERT_NE(track, nullptr) << "Failed to publish track " << track_name;
    EXPECT_TRUE(track->isPublished())
        << "Track was not published: " << track_name;
    EXPECT_EQ(track->info().name, track_name);

    tracks.push_back(std::move(track));
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;

  std::cout
      << "Publishing " << kPublishManyTrackCount << " tracks took "
      << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
      << " ms" << std::endl;
  EXPECT_LT(elapsed, kPublishManyTimeout);

  // This test intentionally creates bursty data-track traffic by pushing a
  // large frame on every published track in quick succession. The RTC sender
  // path uses bounded queues, so under this load not every packet is expected
  // to make it onto the transport and "Failed to enqueue data track packet"
  // logs are expected. The purpose of this test is to verify publish/push
  // behavior and local track state, not end-to-end delivery of every packet.
  for (const auto &track : tracks) {
    EXPECT_TRUE(track->tryPush(
        std::vector<std::uint8_t>(kLargeFramePayloadBytes, 0xFA)))
        << "Failed to push large frame on track " << track->info().name;
    std::this_thread::sleep_for(50ms);
  }

  for (const auto &track : tracks) {
    track->unpublishDataTrack();
    EXPECT_FALSE(track->isPublished());
  }
}

TEST_F(DataTrackE2ETest, PublishDuplicateName) {
  auto rooms = testRooms(1);
  auto &room = rooms[0];

  auto first_track = room->localParticipant()->publishDataTrack("first");
  ASSERT_NE(first_track, nullptr);
  ASSERT_TRUE(first_track->isPublished());

  try {
    (void)room->localParticipant()->publishDataTrack("first");
    FAIL() << "Expected duplicate data-track name to be rejected";
  } catch (const std::runtime_error &error) {
    const std::string message = error.what();
    EXPECT_FALSE(message.empty());
  }

  first_track->unpublishDataTrack();
}

TEST_F(DataTrackE2ETest, CanResubscribeToRemoteDataTrack) {
  const auto track_name = makeTrackName("resubscribe");

  DataTrackPublishedDelegate subscriber_delegate;
  std::vector<TestRoomConnectionOptions> room_configs(2);
  room_configs[1].delegate = &subscriber_delegate;

  auto rooms = testRooms(room_configs);
  auto &publisher_room = rooms[0];

  std::atomic<bool> keep_publishing{true};
  std::exception_ptr publish_error;
  std::thread publisher([&]() {
    try {
      auto track =
          publisher_room->localParticipant()->publishDataTrack(track_name);
      if (!track || !track->isPublished()) {
        throw std::runtime_error("Publisher failed to publish data track");
      }

      while (keep_publishing.load()) {
        if (!track->tryPush(std::vector<std::uint8_t>(64, 0xFA))) {
          throw std::runtime_error("Failed to push resubscribe test frame");
        }
        std::this_thread::sleep_for(50ms);
      }

      track->unpublishDataTrack();
    } catch (...) {
      publish_error = std::current_exception();
    }
  });

  auto remote_track = subscriber_delegate.waitForTrack(kTrackWaitTimeout);
  ASSERT_NE(remote_track, nullptr) << "Timed out waiting for remote data track";

  for (int iteration = 0; iteration < kResubscribeIterations; ++iteration) {
    auto subscription = remote_track->subscribe();
    ASSERT_NE(subscription, nullptr);

    auto frame = readFrameWithTimeout(subscription, 5s);
    EXPECT_FALSE(frame.payload.empty()) << "Iteration " << iteration;

    subscription->close();
    std::this_thread::sleep_for(50ms);
  }

  keep_publishing.store(false);
  publisher.join();

  if (publish_error) {
    std::rethrow_exception(publish_error);
  }
}

TEST_F(DataTrackE2ETest, PreservesUserTimestampEndToEnd) {
  const auto track_name = makeTrackName("user_timestamp");
  const auto sent_timestamp = getTimestampUs();

  DataTrackPublishedDelegate subscriber_delegate;
  std::vector<TestRoomConnectionOptions> room_configs(2);
  room_configs[1].delegate = &subscriber_delegate;

  auto rooms = testRooms(room_configs);
  auto &publisher_room = rooms[0];

  auto local_track =
      publisher_room->localParticipant()->publishDataTrack(track_name);
  ASSERT_NE(local_track, nullptr);
  ASSERT_TRUE(local_track->isPublished());

  auto remote_track = subscriber_delegate.waitForTrack(kTrackWaitTimeout);
  ASSERT_NE(remote_track, nullptr) << "Timed out waiting for remote data track";

  auto subscription = remote_track->subscribe();
  ASSERT_NE(subscription, nullptr);

  std::promise<DataFrame> frame_promise;
  auto frame_future = frame_promise.get_future();
  std::thread reader([&]() {
    try {
      DataFrame frame;
      if (!subscription->read(frame)) {
        throw std::runtime_error(
            "Subscription ended before timestamped frame arrived");
      }
      frame_promise.set_value(std::move(frame));
    } catch (...) {
      frame_promise.set_exception(std::current_exception());
    }
  });

  const bool push_ok =
      local_track->tryPush(std::vector<std::uint8_t>(64, 0xFA), sent_timestamp);
  const auto frame_status = frame_future.wait_for(5s);

  if (frame_status != std::future_status::ready) {
    subscription->close();
  }

  subscription->close();
  reader.join();
  local_track->unpublishDataTrack();

  ASSERT_TRUE(push_ok) << "Failed to push timestamped data frame";
  ASSERT_EQ(frame_status, std::future_status::ready)
      << "Timed out waiting for timestamped frame";

  DataFrame frame;
  try {
    frame = frame_future.get();
  } catch (const std::exception &e) {
    FAIL() << e.what();
  }

  ASSERT_FALSE(frame.payload.empty());
  ASSERT_TRUE(frame.user_timestamp.has_value());
  EXPECT_EQ(frame.user_timestamp.value(), sent_timestamp);
}

TEST_F(DataTrackE2ETest, PublishesAndReceivesEncryptedFramesEndToEnd) {
  const auto track_name = makeTrackName("e2ee_transport");

  DataTrackPublishedDelegate subscriber_delegate;
  auto room_configs = encryptedRoomConfigs(&subscriber_delegate);
  auto rooms = testRooms(room_configs);
  auto &publisher_room = rooms[0];
  auto &subscriber_room = rooms[1];

  ASSERT_NE(publisher_room->e2eeManager(), nullptr);
  ASSERT_NE(subscriber_room->e2eeManager(), nullptr);
  ASSERT_NE(publisher_room->e2eeManager()->keyProvider(), nullptr);
  ASSERT_NE(subscriber_room->e2eeManager()->keyProvider(), nullptr);
  publisher_room->e2eeManager()->setEnabled(true);
  subscriber_room->e2eeManager()->setEnabled(true);
  EXPECT_EQ(publisher_room->e2eeManager()->keyProvider()->exportSharedKey(),
            e2eeSharedKey());
  EXPECT_EQ(subscriber_room->e2eeManager()->keyProvider()->exportSharedKey(),
            e2eeSharedKey());

  auto local_track =
      publisher_room->localParticipant()->publishDataTrack(track_name);
  ASSERT_NE(local_track, nullptr);
  ASSERT_TRUE(local_track->isPublished());
  EXPECT_TRUE(local_track->info().uses_e2ee);

  auto remote_track = subscriber_delegate.waitForTrack(kTrackWaitTimeout);
  ASSERT_NE(remote_track, nullptr) << "Timed out waiting for remote data track";
  EXPECT_TRUE(remote_track->isPublished());
  EXPECT_TRUE(remote_track->info().uses_e2ee);
  EXPECT_EQ(remote_track->info().name, track_name);

  auto subscription = remote_track->subscribe();
  ASSERT_NE(subscription, nullptr);

  std::promise<DataFrame> frame_promise;
  auto frame_future = frame_promise.get_future();
  std::thread reader([&]() {
    try {
      DataFrame frame;
      if (!subscription->read(frame)) {
        throw std::runtime_error(
            "Subscription ended before an encrypted frame arrived");
      }
      frame_promise.set_value(std::move(frame));
    } catch (...) {
      frame_promise.set_exception(std::current_exception());
    }
  });

  bool pushed = false;
  for (int index = 0; index < 200; ++index) {
    std::vector<std::uint8_t> payload(kLargeFramePayloadBytes,
                                      static_cast<std::uint8_t>(index + 1));
    pushed = local_track->tryPush(payload) || pushed;
    if (frame_future.wait_for(25ms) == std::future_status::ready) {
      break;
    }
  }

  const auto frame_status = frame_future.wait_for(5s);
  if (frame_status != std::future_status::ready) {
    subscription->close();
  }
  reader.join();
  ASSERT_TRUE(pushed) << "Failed to push encrypted data frames";
  ASSERT_EQ(frame_status, std::future_status::ready)
      << "Timed out waiting for encrypted frame delivery";

  DataFrame frame;
  try {
    frame = frame_future.get();
  } catch (const std::exception &e) {
    FAIL() << e.what();
  }
  ASSERT_FALSE(frame.payload.empty());
  const auto first_byte = frame.payload.front();
  EXPECT_TRUE(std::all_of(frame.payload.begin(), frame.payload.end(),
                          [first_byte](std::uint8_t byte) {
                            return byte == first_byte;
                          }))
      << "Encrypted payload is not byte-consistent";
  EXPECT_FALSE(frame.user_timestamp.has_value())
      << "Unexpected user timestamp on encrypted frame";

  subscription->close();
  local_track->unpublishDataTrack();
}

TEST_F(DataTrackE2ETest, PreservesUserTimestampOnEncryptedDataTrack) {
  const auto track_name = makeTrackName("e2ee_user_timestamp");
  const auto sent_timestamp = getTimestampUs();
  const std::vector<std::uint8_t> payload(64, 0xFA);

  DataTrackPublishedDelegate subscriber_delegate;
  auto room_configs = encryptedRoomConfigs(&subscriber_delegate);
  auto rooms = testRooms(room_configs);
  auto &publisher_room = rooms[0];
  auto &subscriber_room = rooms[1];

  ASSERT_NE(publisher_room->e2eeManager(), nullptr);
  ASSERT_NE(subscriber_room->e2eeManager(), nullptr);
  publisher_room->e2eeManager()->setEnabled(true);
  subscriber_room->e2eeManager()->setEnabled(true);

  auto local_track =
      publisher_room->localParticipant()->publishDataTrack(track_name);
  ASSERT_NE(local_track, nullptr);
  ASSERT_TRUE(local_track->isPublished());
  EXPECT_TRUE(local_track->info().uses_e2ee);

  auto remote_track = subscriber_delegate.waitForTrack(kTrackWaitTimeout);
  ASSERT_NE(remote_track, nullptr) << "Timed out waiting for remote data track";
  EXPECT_TRUE(remote_track->info().uses_e2ee);

  auto subscription = remote_track->subscribe();
  ASSERT_NE(subscription, nullptr);

  std::promise<DataFrame> frame_promise;
  auto frame_future = frame_promise.get_future();
  std::thread reader([&]() {
    try {
      DataFrame incoming_frame;
      if (!subscription->read(incoming_frame)) {
        throw std::runtime_error(
            "Subscription ended before timestamped encrypted frame arrived");
      }
      frame_promise.set_value(std::move(incoming_frame));
    } catch (...) {
      frame_promise.set_exception(std::current_exception());
    }
  });

  bool pushed = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    pushed = local_track->tryPush(payload, sent_timestamp) || pushed;
    if (frame_future.wait_for(25ms) == std::future_status::ready) {
      break;
    }
  }
  const auto frame_status = frame_future.wait_for(5s);
  if (frame_status != std::future_status::ready) {
    subscription->close();
  }

  reader.join();
  ASSERT_TRUE(pushed) << "Failed to push timestamped encrypted frame";
  ASSERT_EQ(frame_status, std::future_status::ready)
      << "Timed out waiting for timestamped encrypted frame";

  DataFrame frame;
  try {
    frame = frame_future.get();
  } catch (const std::exception &e) {
    FAIL() << e.what();
  }
  EXPECT_EQ(frame.payload, payload);
  ASSERT_TRUE(frame.user_timestamp.has_value());
  EXPECT_EQ(frame.user_timestamp.value(), sent_timestamp);

  subscription->close();
  local_track->unpublishDataTrack();
}

std::string dataTrackParamName(
    const ::testing::TestParamInfo<std::tuple<double, size_t>> &info) {
  if (std::get<0>(info.param) > 100.0) {
    return "HighFpsSinglePacket";
  }
  return "LowFpsMultiPacket";
}

INSTANTIATE_TEST_SUITE_P(DataTrackScenarios, DataTrackTransportTest,
                         ::testing::Values(std::make_tuple(120.0, size_t{8192}),
                                           std::make_tuple(10.0,
                                                           size_t{196608})),
                         dataTrackParamName);

} // namespace test
} // namespace livekit
