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

#include <livekit/data_track_stream.h>
#include <livekit/e2ee.h>
#include <livekit/remote_data_track.h>

#include <condition_variable>
#include <exception>
#include <future>

#include "../common/test_common.h"
#include "ffi_client.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr char kTrackNamePrefix[] = "data_track_e2e";
constexpr auto kTrackWaitTimeout = 10s;
constexpr auto kPollingInterval = 10ms;
constexpr int kResubscribeIterations = 10;
constexpr int kPublishManyTrackCount = 256;
constexpr auto kPublishManyTimeout = 5s;
constexpr std::size_t kLargeFramePayloadBytes = 196608;
constexpr char kE2EESharedSecret[] = "password";
constexpr int kE2EEFrameCount = 5;
constexpr int kTimestampFrameAttempts = 200;

std::string makeTrackName(const std::string& suffix) {
  return std::string(kTrackNamePrefix) + "_" + suffix + "_" + std::to_string(getTimestampUs());
}

std::vector<std::uint8_t> e2eeSharedKey() {
  return std::vector<std::uint8_t>(kE2EESharedSecret, kE2EESharedSecret + sizeof(kE2EESharedSecret) - 1);
}

std::size_t parseTestTrackIndex(const std::string& track_name) {
  constexpr char kPrefix[] = "test_";
  if (track_name.rfind(kPrefix, 0) != 0) {
    throw std::runtime_error("Unexpected test track name: " + track_name);
  }
  return static_cast<std::size_t>(std::stoul(track_name.substr(sizeof(kPrefix) - 1)));
}

E2EEOptions makeE2EEOptions(KeyDerivationFunction key_derivation_function = kDefaultKeyDerivationFunction) {
  E2EEOptions options;
  options.key_provider_options.shared_key = e2eeSharedKey();
  options.key_provider_options.key_derivation_function = key_derivation_function;
  return options;
}

std::vector<TestRoomConnectionOptions> encryptedRoomConfigs(
    RoomDelegate* subscriber_delegate, KeyDerivationFunction key_derivation_function = kDefaultKeyDerivationFunction) {
  std::vector<TestRoomConnectionOptions> room_configs(2);
  room_configs[0].room_options.encryption = makeE2EEOptions(key_derivation_function);
  room_configs[1].room_options.encryption = makeE2EEOptions(key_derivation_function);
  room_configs[1].delegate = subscriber_delegate;
  return room_configs;
}

std::string keyDerivationFunctionName(KeyDerivationFunction key_derivation_function) {
  switch (key_derivation_function) {
    case KeyDerivationFunction::PBKDF2:
      return "PBKDF2";
    case KeyDerivationFunction::HKDF:
      return "HKDF";
    default:
      return "Unknown";
  }
}

template <typename Predicate>
bool waitForCondition(Predicate&& predicate, std::chrono::milliseconds timeout,
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

std::shared_ptr<DataTrackStream> requireSubscription(const std::shared_ptr<RemoteDataTrack>& track) {
  auto result = track->subscribe();
  if (!result) {
    throw std::runtime_error("Failed to subscribe to data track: " + describeDataTrackError(result.error()));
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

  std::vector<std::shared_ptr<RemoteDataTrack>> waitForTracks(std::size_t count, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this, count] { return tracks_.size() >= count; })) {
      return {};
    }
    return {tracks_.begin(), tracks_.begin() + static_cast<std::ptrdiff_t>(count)};
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::shared_ptr<RemoteDataTrack>> tracks_;
};

DataTrackFrame readFrameWithTimeout(const std::shared_ptr<DataTrackStream>& subscription,
                                    std::chrono::milliseconds timeout) {
  std::promise<DataTrackFrame> frame_promise;
  auto future = frame_promise.get_future();

  std::thread reader([subscription, promise = std::move(frame_promise)]() mutable {
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

void runEncryptedDataTrackRoundTrip(KeyDerivationFunction key_derivation_function, const std::string& suffix) {
  const auto track_name = makeTrackName(suffix);

  DataTrackPublishedDelegate subscriber_delegate;
  auto room_configs = encryptedRoomConfigs(&subscriber_delegate, key_derivation_function);
  auto rooms = testRooms(room_configs);
  auto& publisher_room = rooms[0];
  auto& subscriber_room = rooms[1];

  ASSERT_NE(publisher_room->e2eeManager(), nullptr);
  ASSERT_NE(subscriber_room->e2eeManager(), nullptr);
  ASSERT_NE(publisher_room->e2eeManager()->keyProvider(), nullptr);
  ASSERT_NE(subscriber_room->e2eeManager()->keyProvider(), nullptr);
  EXPECT_EQ(publisher_room->e2eeManager()->keyProvider()->options().key_derivation_function, key_derivation_function);
  EXPECT_EQ(subscriber_room->e2eeManager()->keyProvider()->options().key_derivation_function, key_derivation_function);
  publisher_room->e2eeManager()->setEnabled(true);
  subscriber_room->e2eeManager()->setEnabled(true);
  EXPECT_EQ(publisher_room->e2eeManager()->keyProvider()->exportSharedKey(), e2eeSharedKey());
  EXPECT_EQ(subscriber_room->e2eeManager()->keyProvider()->exportSharedKey(), e2eeSharedKey());

  auto publish_result = publisher_room->localParticipant()->publishDataTrack(track_name);
  if (!publish_result) {
    FAIL() << describeDataTrackError(publish_result.error());
  }
  const auto& local_track = publish_result.value();
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
  const auto& subscription = subscribe_result.value();

  std::promise<DataTrackFrame> frame_promise;
  auto frame_future = frame_promise.get_future();
  std::thread reader([&]() {
    try {
      DataTrackFrame frame;
      if (!subscription->read(frame)) {
        throw std::runtime_error("Subscription ended before an encrypted frame arrived");
      }
      frame_promise.set_value(std::move(frame));
    } catch (...) {
      frame_promise.set_exception(std::current_exception());
    }
  });

  bool pushed = false;
  for (int index = 0; index < 200; ++index) {
    std::vector<std::uint8_t> payload(kLargeFramePayloadBytes, static_cast<std::uint8_t>(index + 1));
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
  ASSERT_EQ(frame_status, std::future_status::ready) << "Timed out waiting for encrypted frame delivery";

  DataTrackFrame frame;
  try {
    frame = frame_future.get();
  } catch (const std::exception& e) {
    FAIL() << e.what();
  }
  ASSERT_FALSE(frame.payload.empty());
  const auto first_byte = frame.payload.front();
  EXPECT_TRUE(std::all_of(frame.payload.begin(), frame.payload.end(), [first_byte](std::uint8_t byte) {
    return byte == first_byte;
  })) << "Encrypted payload is not byte-consistent";
  EXPECT_FALSE(frame.user_timestamp.has_value()) << "Unexpected user timestamp on encrypted frame";

  subscription->close();
  local_track->unpublishDataTrack();
}

} // namespace

class DataTrackE2ETest : public LiveKitTestBase {};

class DataTrackKeyDerivationTest : public DataTrackE2ETest,
                                   public ::testing::WithParamInterface<KeyDerivationFunction> {};

TEST_F(DataTrackE2ETest, UnpublishUpdatesPublishedStateEndToEnd) {
  const auto track_name = makeTrackName("published_state");

  DataTrackPublishedDelegate subscriber_delegate;
  std::vector<TestRoomConnectionOptions> room_configs(2);
  room_configs[1].delegate = &subscriber_delegate;

  auto rooms = testRooms(room_configs);
  auto& publisher_room = rooms[0];

  auto publish_result = publisher_room->localParticipant()->publishDataTrack(track_name);
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
  EXPECT_TRUE(waitForCondition([&]() { return !remote_track->isPublished(); }, 2s))
      << "Remote track did not report unpublished state";
}

TEST_F(DataTrackE2ETest, SubscribeAfterUnpublishReportsTerminalError) {
  const auto track_name = makeTrackName("subscribe_after_unpublish");

  DataTrackPublishedDelegate subscriber_delegate;
  std::vector<TestRoomConnectionOptions> room_configs(2);
  room_configs[1].delegate = &subscriber_delegate;

  auto rooms = testRooms(room_configs);
  auto& publisher_room = rooms[0];

  auto local_track = requirePublishedTrack(publisher_room->localParticipant(), track_name);
  ASSERT_TRUE(local_track->isPublished());

  auto remote_track = subscriber_delegate.waitForTrack(kTrackWaitTimeout);
  ASSERT_NE(remote_track, nullptr) << "Timed out waiting for remote data track";
  ASSERT_TRUE(remote_track->isPublished());

  local_track->unpublishDataTrack();
  ASSERT_FALSE(local_track->isPublished());
  ASSERT_TRUE(waitForCondition([&]() { return !remote_track->isPublished(); }, 2s))
      << "Remote track did not report unpublished state";

  auto subscribe_result = remote_track->subscribe();
  if (!subscribe_result) {
    FAIL() << "Expected subscribe to return a stream before terminal EOS: "
           << describeDataTrackError(subscribe_result.error());
  }
  auto subscription = subscribe_result.value();

  std::promise<bool> read_promise;
  auto read_future = read_promise.get_future();
  std::thread reader([subscription, promise = std::move(read_promise)]() mutable {
    DataTrackFrame frame;
    promise.set_value(subscription->read(frame));
  });

  const auto read_status = read_future.wait_for(5s);
  if (read_status != std::future_status::ready) {
    subscription->close();
  }
  reader.join();

  // TODO(BOT-347): this sometimes fails with a timeout.
  ASSERT_EQ(read_status, std::future_status::ready) << "Timed out waiting for terminal data-track EOS";
  EXPECT_FALSE(read_future.get()) << "Unpublished track subscription unexpectedly delivered a frame";

  const auto terminal_error = subscription->terminalError();
  ASSERT_TRUE(terminal_error.has_value()) << "Expected terminal subscribe error on EOS";
  // EXPECT_EQ(terminal_error->code, SubscribeDataTrackErrorCode::UNPUBLISHED);
  // should this actually be internal?
  EXPECT_EQ(terminal_error->code, SubscribeDataTrackErrorCode::INTERNAL);
  EXPECT_FALSE(terminal_error->message.empty());
}

TEST_F(DataTrackE2ETest, PublishManyTracks) {
  auto rooms = testRooms(1);
  auto& room = rooms[0];

  std::vector<std::shared_ptr<LocalDataTrack>> tracks;
  tracks.reserve(kPublishManyTrackCount);

  const auto start = std::chrono::steady_clock::now();
  for (int index = 0; index < kPublishManyTrackCount; ++index) {
    const auto track_name = "track_" + std::to_string(index);
    auto publish_result = room->localParticipant()->publishDataTrack(track_name);
    if (!publish_result) {
      FAIL() << "Failed to publish track " << track_name << ": " << describeDataTrackError(publish_result.error());
    }
    auto track = publish_result.value();
    EXPECT_TRUE(track->isPublished()) << "Track was not published: " << track_name;
    EXPECT_EQ(track->info().name, track_name);

    tracks.push_back(std::move(track));
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;

  std::cout << "Publishing " << kPublishManyTrackCount << " tracks took "
            << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << " ms" << std::endl;
  EXPECT_LT(elapsed, kPublishManyTimeout);

  // This test intentionally creates bursty data-track traffic by pushing a
  // large frame on every published track in quick succession. The RTC sender
  // path uses bounded queues, so under this load not every packet is expected
  // to make it onto the transport and "Failed to enqueue data track packet"
  // logs are expected. The purpose of this test is to verify publish/push
  // behavior and local track state, not end-to-end delivery of every packet.
  for (const auto& track : tracks) {
    auto push_result = track->tryPush(std::vector<std::uint8_t>(kLargeFramePayloadBytes, 0xFA));
    if (!push_result) {
      ADD_FAILURE() << "Failed to push large frame on track " << track->info().name << ": "
                    << describeDataTrackError(push_result.error());
    }
    std::this_thread::sleep_for(50ms);
  }

  for (const auto& track : tracks) {
    track->unpublishDataTrack();
    EXPECT_FALSE(track->isPublished());
  }
}

TEST_F(DataTrackE2ETest, PublishDuplicateName) {
  auto rooms = testRooms(1);
  auto& room = rooms[0];

  auto first_track_result = room->localParticipant()->publishDataTrack("first");
  if (!first_track_result) {
    FAIL() << describeDataTrackError(first_track_result.error());
  }
  auto first_track = first_track_result.value();
  ASSERT_TRUE(first_track->isPublished());

  auto duplicate_result = room->localParticipant()->publishDataTrack("first");
  ASSERT_FALSE(duplicate_result) << "Expected duplicate data-track name to be rejected";
  EXPECT_EQ(duplicate_result.error().code, PublishDataTrackErrorCode::DUPLICATE_NAME);
  EXPECT_FALSE(duplicate_result.error().message.empty());

  first_track->unpublishDataTrack();
}

TEST_F(DataTrackE2ETest, CanResubscribeToRemoteDataTrack) {
  const auto track_name = makeTrackName("resubscribe");

  DataTrackPublishedDelegate subscriber_delegate;
  std::vector<TestRoomConnectionOptions> room_configs(2);
  room_configs[1].delegate = &subscriber_delegate;

  auto rooms = testRooms(room_configs);
  auto& publisher_room = rooms[0];

  std::atomic<bool> keep_publishing{true};
  std::exception_ptr publish_error;
  std::thread publisher([&]() {
    try {
      auto track = requirePublishedTrack(publisher_room->localParticipant(), track_name);
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
  auto& publisher_room = rooms[0];

  std::vector<std::shared_ptr<LocalDataTrack>> local_tracks;
  local_tracks.reserve(kTopicCount);

  for (std::size_t idx = 0; idx < kTopicCount; ++idx) {
    const auto track_name = "test_" + std::to_string(idx);
    auto publish_result = publisher_room->localParticipant()->publishDataTrack(track_name);
    if (!publish_result) {
      FAIL() << "Failed to publish " << track_name << ": " << describeDataTrackError(publish_result.error());
    }
    auto local_track = publish_result.value();
    ASSERT_TRUE(local_track->isPublished()) << track_name;
    local_tracks.push_back(std::move(local_track));
  }

  auto remote_tracks = subscriber_delegate.waitForTracks(kTopicCount, kTrackWaitTimeout);
  ASSERT_EQ(remote_tracks.size(), kTopicCount) << "Timed out waiting for all remote data tracks";

  std::sort(remote_tracks.begin(), remote_tracks.end(),
            [](const std::shared_ptr<RemoteDataTrack>& lhs, const std::shared_ptr<RemoteDataTrack>& rhs) {
              return parseTestTrackIndex(lhs->info().name) < parseTestTrackIndex(rhs->info().name);
            });

  std::vector<FfiHandle> subscription_handles;
  subscription_handles.reserve(kTopicCount);

  for (std::size_t idx = 0; idx < remote_tracks.size(); ++idx) {
    const auto& remote_track = remote_tracks[idx];
    const auto expected_name = "test_" + std::to_string(idx);
    ASSERT_NE(remote_track, nullptr);
    EXPECT_TRUE(remote_track->isPublished()) << expected_name;
    EXPECT_EQ(remote_track->info().name, expected_name);

    const auto subscribe_start = std::chrono::steady_clock::now();
    auto subscribe_result =
        FfiClient::instance().subscribeDataTrack(static_cast<std::uint64_t>(remote_track->testFfiHandleId()));
    const auto subscribe_elapsed = std::chrono::steady_clock::now() - subscribe_start;
    const auto subscribe_elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(subscribe_elapsed).count();

    std::cout << "FfiClient::subscribeDataTrack(" << expected_name << ") completed in " << subscribe_elapsed_ns << " ns"
              << std::endl;

    if (!subscribe_result) {
      FAIL() << "Failed to subscribe to " << expected_name << ": " << describeDataTrackError(subscribe_result.error());
    }

    const auto subscription_handle_id = static_cast<uintptr_t>(subscribe_result.value().handle().id());
    EXPECT_NE(subscription_handle_id, 0u) << expected_name;
    subscription_handles.emplace_back(subscription_handle_id);
    EXPECT_TRUE(subscription_handles.back().valid()) << expected_name;
  }

  for (auto& local_track : local_tracks) {
    local_track->unpublishDataTrack();
  }
}

TEST_F(DataTrackE2ETest, PreservesUserTimestampEndToEnd) {
  const auto track_name = makeTrackName("user_timestamp");
  const auto sent_timestamp = getTimestampUs();

  DataTrackPublishedDelegate subscriber_delegate;
  std::vector<TestRoomConnectionOptions> room_configs(2);
  room_configs[1].delegate = &subscriber_delegate;

  auto rooms = testRooms(room_configs);
  auto& publisher_room = rooms[0];

  auto publish_result = publisher_room->localParticipant()->publishDataTrack(track_name);
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
        throw std::runtime_error("Subscription ended before timestamped frame arrived");
      }
      frame_promise.set_value(std::move(frame));
    } catch (...) {
      frame_promise.set_exception(std::current_exception());
    }
  });

  // Push the same `sent_timestamp` repeatedly so that the test isn't sensitive
  // to a single push being lost while the subscribe pipe is still warming up.
  // Mirrors the livekit/e2e TS suite, which sends the same `userTimestamp` on
  // every frame and asserts every received frame carries that value.
  bool pushed = false;
  for (int attempt = 0; attempt < kTimestampFrameAttempts; ++attempt) {
    auto push_result = local_track->tryPush(std::vector<std::uint8_t>(64, 0xFA), sent_timestamp);
    pushed = static_cast<bool>(push_result) || pushed;
    if (frame_future.wait_for(25ms) == std::future_status::ready) {
      break;
    }
  }
  const auto frame_status = frame_future.wait_for(5s);

  if (frame_status != std::future_status::ready) {
    subscription->close();
  }

  subscription->close();
  reader.join();
  local_track->unpublishDataTrack();

  ASSERT_TRUE(pushed) << "Failed to push timestamped data frame";
  ASSERT_EQ(frame_status, std::future_status::ready) << "Timed out waiting for timestamped frame";

  DataTrackFrame frame;
  try {
    frame = frame_future.get();
  } catch (const std::exception& e) {
    FAIL() << e.what();
  }

  ASSERT_FALSE(frame.payload.empty());
  ASSERT_TRUE(frame.user_timestamp.has_value());
  EXPECT_EQ(frame.user_timestamp.value(), sent_timestamp);
}

TEST_F(DataTrackE2ETest, PublishesAndReceivesEncryptedFramesEndToEnd) {
  runEncryptedDataTrackRoundTrip(kDefaultKeyDerivationFunction, "e2ee_transport");
}

TEST_P(DataTrackKeyDerivationTest, PublishesAndReceivesEncryptedFramesEndToEnd) {
  runEncryptedDataTrackRoundTrip(GetParam(), "e2ee_" + keyDerivationFunctionName(GetParam()));
}

TEST_F(DataTrackE2ETest, PreservesUserTimestampOnEncryptedDataTrack) {
  const auto track_name = makeTrackName("e2ee_user_timestamp");
  const auto sent_timestamp = getTimestampUs();
  const std::vector<std::uint8_t> payload(64, 0xFA);

  DataTrackPublishedDelegate subscriber_delegate;
  auto room_configs = encryptedRoomConfigs(&subscriber_delegate);
  auto rooms = testRooms(room_configs);
  auto& publisher_room = rooms[0];
  auto& subscriber_room = rooms[1];

  ASSERT_NE(publisher_room->e2eeManager(), nullptr);
  ASSERT_NE(subscriber_room->e2eeManager(), nullptr);
  publisher_room->e2eeManager()->setEnabled(true);
  subscriber_room->e2eeManager()->setEnabled(true);

  auto publish_result = publisher_room->localParticipant()->publishDataTrack(track_name);
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
        throw std::runtime_error("Subscription ended before timestamped encrypted frame arrived");
      }
      frame_promise.set_value(std::move(incoming_frame));
    } catch (...) {
      frame_promise.set_exception(std::current_exception());
    }
  });

  bool pushed = false;
  for (int attempt = 0; attempt < kTimestampFrameAttempts; ++attempt) {
    auto payload_copy = payload;
    auto push_result = local_track->tryPush(std::move(payload_copy), sent_timestamp);
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
  ASSERT_EQ(frame_status, std::future_status::ready) << "Timed out waiting for timestamped encrypted frame";

  DataTrackFrame frame;
  try {
    frame = frame_future.get();
  } catch (const std::exception& e) {
    FAIL() << e.what();
  }
  EXPECT_EQ(frame.payload, payload);
  ASSERT_TRUE(frame.user_timestamp.has_value());
  EXPECT_EQ(frame.user_timestamp.value(), sent_timestamp);

  subscription->close();
  local_track->unpublishDataTrack();
}

std::string keyDerivationParamName(const ::testing::TestParamInfo<KeyDerivationFunction>& info) {
  return keyDerivationFunctionName(info.param);
}

INSTANTIATE_TEST_SUITE_P(KeyDerivationFunctions, DataTrackKeyDerivationTest,
                         ::testing::Values(KeyDerivationFunction::PBKDF2, KeyDerivationFunction::HKDF),
                         keyDerivationParamName);

} // namespace livekit::test
