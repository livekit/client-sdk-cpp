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
// a local SFU, set credentials examples/tokens/set_data_track_test_tokens.bash,
// and run:
//   ./build-debug/bin/livekit_integration_tests

#include "../common/test_common.h"

#include "ffi_client.h"

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <future>
#include <livekit/data_track_stream.h>
#include <livekit/e2ee.h>
#include <livekit/remote_data_track.h>
#include <thread>
#include <tuple>

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr char kTrackNamePrefix[] = "data_track_e2e";
constexpr auto kPublishDuration = 10s;
constexpr auto kTrackWaitTimeout = 10s;
constexpr auto kTransportTimeout = kPublishDuration + 25s;
constexpr auto kPollingInterval = 10ms;
constexpr float kMinimumReceivedPercent = 0.9f;
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

std::size_t parseTestTrackIndex(const std::string &track_name) {
  constexpr char kPrefix[] = "test_";
  if (track_name.rfind(kPrefix, 0) != 0) {
    throw std::runtime_error("Unexpected test track name: " + track_name);
  }
  return static_cast<std::size_t>(
      std::stoul(track_name.substr(sizeof(kPrefix) - 1)));
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

template <typename Error>
std::string describeDataTrackError(const Error &error) {
  return "code=" + std::to_string(static_cast<std::uint32_t>(error.code)) +
         " message=" + error.message;
}

std::shared_ptr<LocalDataTrack>
requirePublishedTrack(LocalParticipant *participant, const std::string &name) {
  auto result = participant->publishDataTrack(name);
  if (!result) {
    throw std::runtime_error("Failed to publish data track: " +
                             describeDataTrackError(result.error()));
  }
  return result.value();
}

std::shared_ptr<DataTrackStream>
requireSubscription(const std::shared_ptr<RemoteDataTrack> &track) {
  auto result = track->subscribe();
  if (!result) {
    throw std::runtime_error("Failed to subscribe to data track: " +
                             describeDataTrackError(result.error()));
  }
  return result.value();
}

void requirePushSuccess(const Result<void, LocalDataTrackTryPushError> &result,
                        const std::string &context) {
  if (!result) {
    throw std::runtime_error(context + ": " +
                             describeDataTrackError(result.error()));
  }
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

  std::vector<std::shared_ptr<RemoteDataTrack>>
  waitForTracks(std::size_t count, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout,
                      [this, count] { return tracks_.size() >= count; })) {
      return {};
    }
    return {tracks_.begin(),
            tracks_.begin() + static_cast<std::ptrdiff_t>(count)};
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::shared_ptr<RemoteDataTrack>> tracks_;
};

DataTrackFrame
readFrameWithTimeout(const std::shared_ptr<DataTrackStream> &subscription,
                     std::chrono::milliseconds timeout) {
  std::promise<DataTrackFrame> frame_promise;
  auto future = frame_promise.get_future();

  std::thread reader([subscription,
                      promise = std::move(frame_promise)]() mutable {
    try {
      DataTrackFrame frame;
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

  auto local_track =
      requirePublishedTrack(publisher_room->localParticipant(), track_name);
  ASSERT_TRUE(local_track->isPublished());
  EXPECT_FALSE(local_track->info().uses_e2ee);
  EXPECT_EQ(local_track->info().name, track_name);

  auto remote_track = subscriber_delegate.waitForTrack(kTrackWaitTimeout);
  ASSERT_NE(remote_track, nullptr) << "Timed out waiting for remote data track";
  EXPECT_TRUE(remote_track->isPublished());
  EXPECT_FALSE(remote_track->info().uses_e2ee);
  EXPECT_EQ(remote_track->info().name, track_name);
  EXPECT_EQ(remote_track->publisherIdentity(), publisher_identity);

  auto subscribe_result = remote_track->subscribe();
  if (!subscribe_result) {
    FAIL() << describeDataTrackError(subscribe_result.error());
  }
  auto subscription = subscribe_result.value();

  std::exception_ptr publish_error;
  std::thread publisher([&]() {
    try {
      const auto frame_interval =
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<double>(1.0 / publish_fps));
      auto next_send = std::chrono::steady_clock::now();

      std::cout << "Publishing " << frame_count
                << " frames with payload length " << payload_len << '\n';
      for (size_t index = 0; index < frame_count; ++index) {
        std::vector<std::uint8_t> payload(payload_len,
                                          static_cast<std::uint8_t>(index));
        requirePushSuccess(local_track->tryPush(std::move(payload)),
                           "Failed to push data frame");

        next_send += frame_interval;
        std::this_thread::sleep_until(next_send);
      }

      local_track->unpublishDataTrack();
    } catch (...) {
      publish_error = std::current_exception();
    }
  });

  const auto receive_min = static_cast<size_t>(
      static_cast<float>(frame_count) * kMinimumReceivedPercent);
  std::promise<size_t> receive_count_promise;
  auto receive_count_future = receive_count_promise.get_future();
  std::exception_ptr subscribe_error;
  std::thread subscriber([&]() {
    try {
      size_t received_count = 0;
      DataTrackFrame frame;
      while (subscription->read(frame) && received_count < receive_min) {
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

  if (receive_count_future.wait_for(kTransportTimeout) !=
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
            << " frames (" << received_percent * 100.0f << "%)" << '\n';

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

  auto publish_result =
      publisher_room->localParticipant()->publishDataTrack(track_name);
  if (!publish_result) {
    FAIL() << describeDataTrackError(publish_result.error());
  }
  auto local_track = publish_result.value();
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
    auto publish_result =
        room->localParticipant()->publishDataTrack(track_name);
    if (!publish_result) {
      FAIL() << "Failed to publish track " << track_name << ": "
             << describeDataTrackError(publish_result.error());
    }
    auto track = publish_result.value();
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
    auto push_result = track->tryPush(
        std::vector<std::uint8_t>(kLargeFramePayloadBytes, 0xFA));
    if (!push_result) {
      ADD_FAILURE() << "Failed to push large frame on track "
                    << track->info().name << ": "
                    << describeDataTrackError(push_result.error());
    }
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

  auto first_track_result = room->localParticipant()->publishDataTrack("first");
  if (!first_track_result) {
    FAIL() << describeDataTrackError(first_track_result.error());
  }
  auto first_track = first_track_result.value();
  ASSERT_TRUE(first_track->isPublished());

  auto duplicate_result = room->localParticipant()->publishDataTrack("first");
  ASSERT_FALSE(duplicate_result)
      << "Expected duplicate data-track name to be rejected";
  EXPECT_EQ(duplicate_result.error().code,
            PublishDataTrackErrorCode::DUPLICATE_NAME);
  EXPECT_FALSE(duplicate_result.error().message.empty());

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
          requirePublishedTrack(publisher_room->localParticipant(), track_name);
      if (!track->isPublished()) {
        throw std::runtime_error("Publisher failed to publish data track");
      }

      while (keep_publishing.load()) {
        requirePushSuccess(track->tryPush(std::vector<std::uint8_t>(64, 0xFA)),
                           "Failed to push resubscribe test frame");
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
    auto subscribe_result = remote_track->subscribe();
    if (!subscribe_result) {
      FAIL() << describeDataTrackError(subscribe_result.error());
    }
    auto subscription = subscribe_result.value();

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

TEST_F(DataTrackE2ETest, FfiClientSubscribeDataTrackReturnsSyncResult) {
  constexpr std::size_t kTopicCount = 20;

  DataTrackPublishedDelegate subscriber_delegate;
  std::vector<TestRoomConnectionOptions> room_configs(2);
  room_configs[1].delegate = &subscriber_delegate;

  auto rooms = testRooms(room_configs);
  auto &publisher_room = rooms[0];

  std::vector<std::shared_ptr<LocalDataTrack>> local_tracks;
  local_tracks.reserve(kTopicCount);

  for (std::size_t idx = 0; idx < kTopicCount; ++idx) {
    const auto track_name = "test_" + std::to_string(idx);
    auto publish_result =
        publisher_room->localParticipant()->publishDataTrack(track_name);
    if (!publish_result) {
      FAIL() << "Failed to publish " << track_name << ": "
             << describeDataTrackError(publish_result.error());
    }
    auto local_track = publish_result.value();
    ASSERT_TRUE(local_track->isPublished()) << track_name;
    local_tracks.push_back(std::move(local_track));
  }

  auto remote_tracks =
      subscriber_delegate.waitForTracks(kTopicCount, kTrackWaitTimeout);
  ASSERT_EQ(remote_tracks.size(), kTopicCount)
      << "Timed out waiting for all remote data tracks";

  std::sort(remote_tracks.begin(), remote_tracks.end(),
            [](const std::shared_ptr<RemoteDataTrack> &lhs,
               const std::shared_ptr<RemoteDataTrack> &rhs) {
              return parseTestTrackIndex(lhs->info().name) <
                     parseTestTrackIndex(rhs->info().name);
            });

  std::vector<FfiHandle> subscription_handles;
  subscription_handles.reserve(kTopicCount);

  for (std::size_t idx = 0; idx < remote_tracks.size(); ++idx) {
    const auto &remote_track = remote_tracks[idx];
    const auto expected_name = "test_" + std::to_string(idx);
    ASSERT_NE(remote_track, nullptr);
    EXPECT_TRUE(remote_track->isPublished()) << expected_name;
    EXPECT_EQ(remote_track->info().name, expected_name);

    const auto subscribe_start = std::chrono::steady_clock::now();
    auto subscribe_result = FfiClient::instance().subscribeDataTrack(
        static_cast<std::uint64_t>(remote_track->testFfiHandleId()));
    const auto subscribe_elapsed =
        std::chrono::steady_clock::now() - subscribe_start;
    const auto subscribe_elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(subscribe_elapsed)
            .count();

    std::cout << "FfiClient::subscribeDataTrack(" << expected_name
              << ") completed in " << subscribe_elapsed_ns << " ns"
              << std::endl;

    if (!subscribe_result) {
      FAIL() << "Failed to subscribe to " << expected_name << ": "
             << describeDataTrackError(subscribe_result.error());
    }

    const auto subscription_handle_id =
        static_cast<uintptr_t>(subscribe_result.value().handle().id());
    EXPECT_NE(subscription_handle_id, 0u) << expected_name;
    subscription_handles.emplace_back(subscription_handle_id);
    EXPECT_TRUE(subscription_handles.back().valid()) << expected_name;
  }

  for (auto &local_track : local_tracks) {
    local_track->unpublishDataTrack();
  }
}

TEST_F(DataTrackE2ETest, PreservesUserTimestampEndToEnd) {
  const auto track_name = makeTrackName("user_timestamp");

  DataTrackPublishedDelegate subscriber_delegate;
  std::vector<TestRoomConnectionOptions> room_configs(2);
  room_configs[1].delegate = &subscriber_delegate;

  auto rooms = testRooms(room_configs);
  auto &publisher_room = rooms[0];

  auto publish_result =
      publisher_room->localParticipant()->publishDataTrack(track_name);
  if (!publish_result) {
    FAIL() << describeDataTrackError(publish_result.error());
  }
  auto local_track = publish_result.value();
  ASSERT_TRUE(local_track->isPublished());

  auto remote_track = subscriber_delegate.waitForTrack(kTrackWaitTimeout);
  ASSERT_NE(remote_track, nullptr) << "Timed out waiting for remote data track";

  auto subscribe_result = remote_track->subscribe();
  if (!subscribe_result) {
    FAIL() << describeDataTrackError(subscribe_result.error());
  }
  auto subscription = subscribe_result.value();

  std::promise<DataTrackFrame> frame_promise;
  auto frame_future = frame_promise.get_future();
  std::thread reader([&]() {
    try {
      DataTrackFrame frame;
      if (!subscription->read(frame)) {
        throw std::runtime_error(
            "Subscription ended before timestamped frame arrived");
      }
      frame_promise.set_value(std::move(frame));
    } catch (...) {
      frame_promise.set_exception(std::current_exception());
    }
  });

  std::atomic<bool> publishing{true};
  std::string push_error;
  std::thread publisher([&]() {
    while (publishing.load(std::memory_order_relaxed)) {
      auto push_result =
          local_track->tryPush(std::vector<std::uint8_t>(64, 0xFA),
                               getTimestampUs());
      if (!push_result) {
        push_error = describeDataTrackError(push_result.error());
        publishing.store(false, std::memory_order_relaxed);
        return;
      }
      std::this_thread::sleep_for(50ms);
    }
  });

  const auto frame_status = frame_future.wait_for(5s);
  publishing.store(false, std::memory_order_relaxed);

  if (frame_status != std::future_status::ready) {
    subscription->close();
  }

  subscription->close();
  publisher.join();
  reader.join();
  local_track->unpublishDataTrack();

  if (!push_error.empty()) {
    FAIL() << "Failed to push timestamped data frame: " << push_error;
  }
  ASSERT_EQ(frame_status, std::future_status::ready)
      << "Timed out waiting for timestamped frame";

  DataTrackFrame frame;
  try {
    frame = frame_future.get();
  } catch (const std::exception &e) {
    FAIL() << e.what();
  }

  ASSERT_FALSE(frame.payload.empty());
  ASSERT_TRUE(frame.user_timestamp.has_value());
  const auto received_at = getTimestampUs();
  ASSERT_LE(frame.user_timestamp.value(), received_at);
  EXPECT_LT(received_at - frame.user_timestamp.value(), 1000000u);
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

  auto publish_result =
      publisher_room->localParticipant()->publishDataTrack(track_name);
  if (!publish_result) {
    FAIL() << describeDataTrackError(publish_result.error());
  }
  auto local_track = publish_result.value();
  ASSERT_TRUE(local_track->isPublished());
  EXPECT_TRUE(local_track->info().uses_e2ee);

  auto remote_track = subscriber_delegate.waitForTrack(kTrackWaitTimeout);
  ASSERT_NE(remote_track, nullptr) << "Timed out waiting for remote data track";
  EXPECT_TRUE(remote_track->isPublished());
  EXPECT_TRUE(remote_track->info().uses_e2ee);
  EXPECT_EQ(remote_track->info().name, track_name);

  auto subscribe_result = remote_track->subscribe();
  if (!subscribe_result) {
    FAIL() << describeDataTrackError(subscribe_result.error());
  }
  auto subscription = subscribe_result.value();

  std::promise<DataTrackFrame> frame_promise;
  auto frame_future = frame_promise.get_future();
  std::thread reader([&]() {
    try {
      DataTrackFrame frame;
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
    auto push_result = local_track->tryPush(std::move(payload));
    pushed = static_cast<bool>(push_result) || pushed;
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

  DataTrackFrame frame;
  try {
    frame = frame_future.get();
  } catch (const std::exception &e) {
    FAIL() << e.what();
  }
  ASSERT_FALSE(frame.payload.empty());
  const auto first_byte = frame.payload.front();
  EXPECT_TRUE(std::all_of(
      frame.payload.begin(), frame.payload.end(),
      [first_byte](std::uint8_t byte) { return byte == first_byte; }))
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

  auto publish_result =
      publisher_room->localParticipant()->publishDataTrack(track_name);
  if (!publish_result) {
    FAIL() << describeDataTrackError(publish_result.error());
  }
  auto local_track = publish_result.value();
  ASSERT_TRUE(local_track->isPublished());
  EXPECT_TRUE(local_track->info().uses_e2ee);

  auto remote_track = subscriber_delegate.waitForTrack(kTrackWaitTimeout);
  ASSERT_NE(remote_track, nullptr) << "Timed out waiting for remote data track";
  EXPECT_TRUE(remote_track->info().uses_e2ee);

  auto subscribe_result = remote_track->subscribe();
  if (!subscribe_result) {
    FAIL() << describeDataTrackError(subscribe_result.error());
  }
  auto subscription = subscribe_result.value();

  std::promise<DataTrackFrame> frame_promise;
  auto frame_future = frame_promise.get_future();
  std::thread reader([&]() {
    try {
      DataTrackFrame incoming_frame;
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
    auto payload_copy = payload;
    auto push_result =
        local_track->tryPush(std::move(payload_copy), sent_timestamp);
    pushed = static_cast<bool>(push_result) || pushed;
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

  DataTrackFrame frame;
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

} // namespace livekit::test
