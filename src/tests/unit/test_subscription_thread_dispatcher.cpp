/*
 * Copyright 2025 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/// @file test_subscription_thread_dispatcher.cpp
/// @brief Unit tests for SubscriptionThreadDispatcher registration state.

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>

namespace livekit {

class SubscriptionThreadDispatcherTest : public ::testing::Test {
protected:
  void SetUp() override {
    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);
  }

  void TearDown() override { livekit::shutdown(); }

  using CallbackKey = SubscriptionThreadDispatcher::CallbackKey;
  using CallbackKeyHash = SubscriptionThreadDispatcher::CallbackKeyHash;
  using DataCallbackKey = SubscriptionThreadDispatcher::DataCallbackKey;
  using DataCallbackKeyHash = SubscriptionThreadDispatcher::DataCallbackKeyHash;

  static auto &audioCallbacks(SubscriptionThreadDispatcher &dispatcher) {
    return dispatcher.audio_callbacks_;
  }
  static auto &videoCallbacks(SubscriptionThreadDispatcher &dispatcher) {
    return dispatcher.video_callbacks_;
  }
  static auto &activeReaders(SubscriptionThreadDispatcher &dispatcher) {
    return dispatcher.active_readers_;
  }
  static auto &dataCallbacks(SubscriptionThreadDispatcher &dispatcher) {
    return dispatcher.data_callbacks_;
  }
  static auto &activeDataReaders(SubscriptionThreadDispatcher &dispatcher) {
    return dispatcher.active_data_readers_;
  }
  static auto &remoteDataTracks(SubscriptionThreadDispatcher &dispatcher) {
    return dispatcher.remote_data_tracks_;
  }
  static int maxActiveReaders() {
    return SubscriptionThreadDispatcher::kMaxActiveReaders;
  }
};

// ============================================================================
// CallbackKey equality
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyEqualKeysCompareEqual) {
  CallbackKey a{"alice", TrackSource::SOURCE_MICROPHONE, ""};
  CallbackKey b{"alice", TrackSource::SOURCE_MICROPHONE, ""};
  EXPECT_TRUE(a == b);
}

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyDifferentIdentityNotEqual) {
  CallbackKey a{"alice", TrackSource::SOURCE_MICROPHONE, ""};
  CallbackKey b{"bob", TrackSource::SOURCE_MICROPHONE, ""};
  EXPECT_FALSE(a == b);
}

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyDifferentSourceNotEqual) {
  CallbackKey a{"alice", TrackSource::SOURCE_MICROPHONE, ""};
  CallbackKey b{"alice", TrackSource::SOURCE_CAMERA, ""};
  EXPECT_FALSE(a == b);
}

TEST_F(SubscriptionThreadDispatcherTest,
       CallbackKeyDifferentTrackNameNotEqual) {
  CallbackKey a{"alice", TrackSource::SOURCE_UNKNOWN, "cam-main"};
  CallbackKey b{"alice", TrackSource::SOURCE_UNKNOWN, "cam-backup"};
  EXPECT_FALSE(a == b);
}

// ============================================================================
// CallbackKeyHash
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest,
       CallbackKeyHashEqualKeysProduceSameHash) {
  CallbackKey a{"alice", TrackSource::SOURCE_MICROPHONE, ""};
  CallbackKey b{"alice", TrackSource::SOURCE_MICROPHONE, ""};
  CallbackKeyHash hasher;
  EXPECT_EQ(hasher(a), hasher(b));
}

TEST_F(SubscriptionThreadDispatcherTest,
       CallbackKeyHashDifferentKeysLikelyDifferentHash) {
  CallbackKeyHash hasher;
  CallbackKey mic{"alice", TrackSource::SOURCE_MICROPHONE, ""};
  CallbackKey cam{"alice", TrackSource::SOURCE_CAMERA, ""};
  CallbackKey bob{"bob", TrackSource::SOURCE_MICROPHONE, ""};
  CallbackKey named{"alice", TrackSource::SOURCE_UNKNOWN, "mic-main"};

  EXPECT_NE(hasher(mic), hasher(cam));
  EXPECT_NE(hasher(mic), hasher(bob));
  EXPECT_NE(hasher(mic), hasher(named));
}

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyWorksAsUnorderedMapKey) {
  std::unordered_map<CallbackKey, int, CallbackKeyHash> map;

  CallbackKey k1{"alice", TrackSource::SOURCE_MICROPHONE, ""};
  CallbackKey k2{"bob", TrackSource::SOURCE_CAMERA, ""};
  CallbackKey k3{"alice", TrackSource::SOURCE_CAMERA, ""};

  map[k1] = 1;
  map[k2] = 2;
  map[k3] = 3;

  EXPECT_EQ(map.size(), 3u);
  EXPECT_EQ(map[k1], 1);
  EXPECT_EQ(map[k2], 2);
  EXPECT_EQ(map[k3], 3);

  map[k1] = 42;
  EXPECT_EQ(map[k1], 42);
  EXPECT_EQ(map.size(), 3u);

  map.erase(k2);
  EXPECT_EQ(map.size(), 2u);
  EXPECT_EQ(map.count(k2), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyEmptyIdentityWorks) {
  CallbackKey a{"", TrackSource::SOURCE_UNKNOWN, ""};
  CallbackKey b{"", TrackSource::SOURCE_UNKNOWN, ""};
  CallbackKeyHash hasher;
  EXPECT_TRUE(a == b);
  EXPECT_EQ(hasher(a), hasher(b));
}

// ============================================================================
// kMaxActiveReaders
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, MaxActiveReadersIs20) {
  EXPECT_EQ(maxActiveReaders(), 20);
}

// ============================================================================
// Registration and clearing
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, SetAudioCallbackStoresRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                     [](const AudioFrame &) {});

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest,
       SetAudioCallbackByTrackNameStoresRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic-main",
                                     [](const AudioFrame &) {});

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
  EXPECT_EQ(
      audioCallbacks(dispatcher)
          .count(CallbackKey{"alice", TrackSource::SOURCE_UNKNOWN, "mic-main"}),
      1u);
}

TEST_F(SubscriptionThreadDispatcherTest, SetVideoCallbackStoresRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                     [](const VideoFrame &, std::int64_t) {});

  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest,
       SetVideoCallbackByTrackNameStoresRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", "cam-main",
                                     [](const VideoFrame &, std::int64_t) {});

  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
  EXPECT_EQ(
      videoCallbacks(dispatcher)
          .count(CallbackKey{"alice", TrackSource::SOURCE_UNKNOWN, "cam-main"}),
      1u);
}

TEST_F(SubscriptionThreadDispatcherTest,
       ClearAudioCallbackRemovesRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                     [](const AudioFrame &) {});
  ASSERT_EQ(audioCallbacks(dispatcher).size(), 1u);

  dispatcher.clearOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE);
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest,
       ClearAudioCallbackByTrackNameRemovesRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic-main",
                                     [](const AudioFrame &) {});
  ASSERT_EQ(audioCallbacks(dispatcher).size(), 1u);

  dispatcher.clearOnAudioFrameCallback("alice", "mic-main");
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest,
       ClearVideoCallbackRemovesRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                     [](const VideoFrame &, std::int64_t) {});
  ASSERT_EQ(videoCallbacks(dispatcher).size(), 1u);

  dispatcher.clearOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA);
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest,
       ClearVideoCallbackByTrackNameRemovesRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", "cam-main",
                                     [](const VideoFrame &, std::int64_t) {});
  ASSERT_EQ(videoCallbacks(dispatcher).size(), 1u);

  dispatcher.clearOnVideoFrameCallback("alice", "cam-main");
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearNonExistentCallbackIsNoOp) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_NO_THROW(dispatcher.clearOnAudioFrameCallback(
      "nobody", TrackSource::SOURCE_MICROPHONE));
  EXPECT_NO_THROW(dispatcher.clearOnVideoFrameCallback(
      "nobody", TrackSource::SOURCE_CAMERA));
  EXPECT_NO_THROW(dispatcher.clearOnAudioFrameCallback("nobody", "missing"));
  EXPECT_NO_THROW(dispatcher.clearOnVideoFrameCallback("nobody", "missing"));
}

TEST_F(SubscriptionThreadDispatcherTest,
       OverwriteAudioCallbackKeepsSingleEntry) {
  SubscriptionThreadDispatcher dispatcher;
  std::atomic<int> counter1{0};
  std::atomic<int> counter2{0};

  dispatcher.setOnAudioFrameCallback(
      "alice", TrackSource::SOURCE_MICROPHONE,
      [&counter1](const AudioFrame &) { counter1++; });
  dispatcher.setOnAudioFrameCallback(
      "alice", TrackSource::SOURCE_MICROPHONE,
      [&counter2](const AudioFrame &) { counter2++; });

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u)
      << "Re-registering with the same key should overwrite, not add";
}

TEST_F(SubscriptionThreadDispatcherTest,
       OverwriteVideoCallbackKeepsSingleEntry) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                     [](const VideoFrame &, std::int64_t) {});
  dispatcher.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                     [](const VideoFrame &, std::int64_t) {});

  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest,
       OverwriteTrackNameAudioCallbackKeepsSingleEntry) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic-main",
                                     [](const AudioFrame &) {});
  dispatcher.setOnAudioFrameCallback("alice", "mic-main",
                                     [](const AudioFrame &) {});

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest,
       MultipleDistinctCallbacksAreIndependent) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                     [](const AudioFrame &) {});
  dispatcher.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                     [](const VideoFrame &, std::int64_t) {});
  dispatcher.setOnAudioFrameCallback("bob", TrackSource::SOURCE_MICROPHONE,
                                     [](const AudioFrame &) {});
  dispatcher.setOnVideoFrameCallback("bob", TrackSource::SOURCE_CAMERA,
                                     [](const VideoFrame &, std::int64_t) {});

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 2u);
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 2u);

  dispatcher.clearOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE);
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 2u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearingOneSourceDoesNotAffectOther) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                     [](const AudioFrame &) {});
  dispatcher.setOnAudioFrameCallback("alice",
                                     TrackSource::SOURCE_SCREENSHARE_AUDIO,
                                     [](const AudioFrame &) {});
  ASSERT_EQ(audioCallbacks(dispatcher).size(), 2u);

  dispatcher.clearOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE);
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);

  CallbackKey remaining{"alice", TrackSource::SOURCE_SCREENSHARE_AUDIO, ""};
  EXPECT_EQ(audioCallbacks(dispatcher).count(remaining), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest,
       SourceAndTrackNameAudioCallbacksAreIndependent) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                     [](const AudioFrame &) {});
  dispatcher.setOnAudioFrameCallback("alice", "mic-main",
                                     [](const AudioFrame &) {});
  ASSERT_EQ(audioCallbacks(dispatcher).size(), 2u);

  dispatcher.clearOnAudioFrameCallback("alice", "mic-main");
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
  EXPECT_EQ(
      audioCallbacks(dispatcher)
          .count(CallbackKey{"alice", TrackSource::SOURCE_MICROPHONE, ""}),
      1u);
}

// ============================================================================
// Active readers state (no real streams, just map state)
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, NoActiveReadersInitially) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_TRUE(activeReaders(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest,
       ActiveReadersEmptyAfterCallbackRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                     [](const AudioFrame &) {});
  EXPECT_TRUE(activeReaders(dispatcher).empty())
      << "Registering a callback without a subscribed track should not spawn "
         "readers";
}

// ============================================================================
// Destruction safety
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest,
       DestroyDispatcherWithRegisteredCallbacksIsSafe) {
  EXPECT_NO_THROW({
    SubscriptionThreadDispatcher dispatcher;
    dispatcher.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                       [](const AudioFrame &) {});
    dispatcher.setOnVideoFrameCallback("bob", TrackSource::SOURCE_CAMERA,
                                       [](const VideoFrame &, std::int64_t) {});
  });
}

TEST_F(SubscriptionThreadDispatcherTest,
       DestroyDispatcherAfterClearingCallbacksIsSafe) {
  EXPECT_NO_THROW({
    SubscriptionThreadDispatcher dispatcher;
    dispatcher.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                       [](const AudioFrame &) {});
    dispatcher.clearOnAudioFrameCallback("alice",
                                         TrackSource::SOURCE_MICROPHONE);
  });
}

// ============================================================================
// Thread-safety of registration/clearing
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, ConcurrentRegistrationDoesNotCrash) {
  SubscriptionThreadDispatcher dispatcher;
  constexpr int kThreads = 8;
  constexpr int kIterations = 100;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&dispatcher, t]() {
      for (int i = 0; i < kIterations; ++i) {
        std::string id = "participant-" + std::to_string(t);
        dispatcher.setOnAudioFrameCallback(id, TrackSource::SOURCE_MICROPHONE,
                                           [](const AudioFrame &) {});
        dispatcher.clearOnAudioFrameCallback(id,
                                             TrackSource::SOURCE_MICROPHONE);
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  EXPECT_TRUE(audioCallbacks(dispatcher).empty())
      << "All callbacks should be cleared after concurrent register/clear";
}

TEST_F(SubscriptionThreadDispatcherTest,
       ConcurrentMixedAudioVideoRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  constexpr int kThreads = 4;
  constexpr int kIterations = 50;

  std::vector<std::thread> threads;

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&dispatcher, t]() {
      std::string id = "p-" + std::to_string(t);
      for (int i = 0; i < kIterations; ++i) {
        dispatcher.setOnAudioFrameCallback(id, TrackSource::SOURCE_MICROPHONE,
                                           [](const AudioFrame &) {});
        dispatcher.setOnVideoFrameCallback(
            id, TrackSource::SOURCE_CAMERA,
            [](const VideoFrame &, std::int64_t) {});
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  EXPECT_EQ(audioCallbacks(dispatcher).size(), static_cast<size_t>(kThreads));
  EXPECT_EQ(videoCallbacks(dispatcher).size(), static_cast<size_t>(kThreads));
}

// ============================================================================
// Bulk registration
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, ManyDistinctCallbacksCanBeRegistered) {
  SubscriptionThreadDispatcher dispatcher;
  constexpr int kCount = 50;

  for (int i = 0; i < kCount; ++i) {
    dispatcher.setOnAudioFrameCallback("participant-" + std::to_string(i),
                                       TrackSource::SOURCE_MICROPHONE,
                                       [](const AudioFrame &) {});
  }

  EXPECT_EQ(audioCallbacks(dispatcher).size(), static_cast<size_t>(kCount));

  for (int i = 0; i < kCount; ++i) {
    dispatcher.clearOnAudioFrameCallback("participant-" + std::to_string(i),
                                         TrackSource::SOURCE_MICROPHONE);
  }

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 0u);
}

// ============================================================================
// DataCallbackKey equality
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, DataCallbackKeyEqualKeysCompareEqual) {
  DataCallbackKey a{"alice", "my-track"};
  DataCallbackKey b{"alice", "my-track"};
  EXPECT_TRUE(a == b);
}

TEST_F(SubscriptionThreadDispatcherTest,
       DataCallbackKeyDifferentIdentityNotEqual) {
  DataCallbackKey a{"alice", "my-track"};
  DataCallbackKey b{"bob", "my-track"};
  EXPECT_FALSE(a == b);
}

TEST_F(SubscriptionThreadDispatcherTest,
       DataCallbackKeyDifferentTrackNameNotEqual) {
  DataCallbackKey a{"alice", "track-a"};
  DataCallbackKey b{"alice", "track-b"};
  EXPECT_FALSE(a == b);
}

// ============================================================================
// DataCallbackKeyHash
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest,
       DataCallbackKeyHashEqualKeysProduceSameHash) {
  DataCallbackKey a{"alice", "my-track"};
  DataCallbackKey b{"alice", "my-track"};
  DataCallbackKeyHash hasher;
  EXPECT_EQ(hasher(a), hasher(b));
}

TEST_F(SubscriptionThreadDispatcherTest,
       DataCallbackKeyHashDifferentKeysLikelyDifferentHash) {
  DataCallbackKeyHash hasher;
  DataCallbackKey a{"alice", "track-a"};
  DataCallbackKey b{"alice", "track-b"};
  DataCallbackKey c{"bob", "track-a"};
  EXPECT_NE(hasher(a), hasher(b));
  EXPECT_NE(hasher(a), hasher(c));
}

TEST_F(SubscriptionThreadDispatcherTest,
       DataCallbackKeyWorksAsUnorderedMapKey) {
  std::unordered_map<DataCallbackKey, int, DataCallbackKeyHash> map;

  DataCallbackKey k1{"alice", "track-a"};
  DataCallbackKey k2{"bob", "track-b"};
  DataCallbackKey k3{"alice", "track-b"};

  map[k1] = 1;
  map[k2] = 2;
  map[k3] = 3;

  EXPECT_EQ(map.size(), 3u);
  EXPECT_EQ(map[k1], 1);
  EXPECT_EQ(map[k2], 2);
  EXPECT_EQ(map[k3], 3);

  map[k1] = 42;
  EXPECT_EQ(map[k1], 42);
  EXPECT_EQ(map.size(), 3u);

  map.erase(k2);
  EXPECT_EQ(map.size(), 2u);
  EXPECT_EQ(map.count(k2), 0u);
}

// ============================================================================
// Data callback registration and clearing
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest,
       AddDataFrameCallbackStoresRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  auto id = dispatcher.addOnDataFrameCallback(
      "alice", "my-track",
      [](const std::vector<std::uint8_t> &, std::optional<std::uint64_t>) {});

  EXPECT_NE(id, 0u);
  EXPECT_EQ(dataCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest,
       RemoveDataFrameCallbackRemovesRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  auto id = dispatcher.addOnDataFrameCallback(
      "alice", "my-track",
      [](const std::vector<std::uint8_t> &, std::optional<std::uint64_t>) {});
  ASSERT_EQ(dataCallbacks(dispatcher).size(), 1u);

  dispatcher.removeOnDataFrameCallback(id);
  EXPECT_EQ(dataCallbacks(dispatcher).size(), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, RemoveNonExistentDataCallbackIsNoOp) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_NO_THROW(dispatcher.removeOnDataFrameCallback(999));
}

TEST_F(SubscriptionThreadDispatcherTest,
       MultipleDataCallbacksForSameKeyAreIndependent) {
  SubscriptionThreadDispatcher dispatcher;
  auto cb = [](const std::vector<std::uint8_t> &,
               std::optional<std::uint64_t>) {};
  auto id1 = dispatcher.addOnDataFrameCallback("alice", "track", cb);
  auto id2 = dispatcher.addOnDataFrameCallback("alice", "track", cb);

  EXPECT_NE(id1, id2);
  EXPECT_EQ(dataCallbacks(dispatcher).size(), 2u);

  dispatcher.removeOnDataFrameCallback(id1);
  EXPECT_EQ(dataCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest,
       DataCallbackIdsAreMonotonicallyIncreasing) {
  SubscriptionThreadDispatcher dispatcher;
  auto cb = [](const std::vector<std::uint8_t> &,
               std::optional<std::uint64_t>) {};
  auto id1 = dispatcher.addOnDataFrameCallback("alice", "t1", cb);
  auto id2 = dispatcher.addOnDataFrameCallback("bob", "t2", cb);
  auto id3 = dispatcher.addOnDataFrameCallback("carol", "t3", cb);

  EXPECT_LT(id1, id2);
  EXPECT_LT(id2, id3);
}

// ============================================================================
// Data track active readers (no real tracks, just map state)
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, NoActiveDataReadersInitially) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_TRUE(activeDataReaders(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest,
       ActiveDataReadersEmptyAfterCallbackRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.addOnDataFrameCallback(
      "alice", "my-track",
      [](const std::vector<std::uint8_t> &, std::optional<std::uint64_t>) {});
  EXPECT_TRUE(activeDataReaders(dispatcher).empty())
      << "Registering a callback without a published track should not spawn "
         "readers";
}

TEST_F(SubscriptionThreadDispatcherTest, NoRemoteDataTracksInitially) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_TRUE(remoteDataTracks(dispatcher).empty());
}

// ============================================================================
// Data track destruction safety
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest,
       DestroyDispatcherWithDataCallbacksIsSafe) {
  EXPECT_NO_THROW({
    SubscriptionThreadDispatcher dispatcher;
    dispatcher.addOnDataFrameCallback(
        "alice", "track-a",
        [](const std::vector<std::uint8_t> &, std::optional<std::uint64_t>) {});
    dispatcher.addOnDataFrameCallback(
        "bob", "track-b",
        [](const std::vector<std::uint8_t> &, std::optional<std::uint64_t>) {});
  });
}

TEST_F(SubscriptionThreadDispatcherTest,
       DestroyDispatcherAfterRemovingDataCallbacksIsSafe) {
  EXPECT_NO_THROW({
    SubscriptionThreadDispatcher dispatcher;
    auto id = dispatcher.addOnDataFrameCallback(
        "alice", "track-a",
        [](const std::vector<std::uint8_t> &, std::optional<std::uint64_t>) {});
    dispatcher.removeOnDataFrameCallback(id);
  });
}

// ============================================================================
// Mixed audio/video/data registration
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest,
       MixedAudioVideoDataCallbacksAreIndependent) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                     [](const AudioFrame &) {});
  dispatcher.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                     [](const VideoFrame &, std::int64_t) {});
  dispatcher.addOnDataFrameCallback(
      "alice", "data-track",
      [](const std::vector<std::uint8_t> &, std::optional<std::uint64_t>) {});

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
  EXPECT_EQ(dataCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, StopAllClearsDataCallbacksAndReaders) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.addOnDataFrameCallback(
      "alice", "track-a",
      [](const std::vector<std::uint8_t> &, std::optional<std::uint64_t>) {});
  dispatcher.addOnDataFrameCallback(
      "bob", "track-b",
      [](const std::vector<std::uint8_t> &, std::optional<std::uint64_t>) {});

  dispatcher.stopAll();

  EXPECT_EQ(dataCallbacks(dispatcher).size(), 0u);
  EXPECT_TRUE(activeDataReaders(dispatcher).empty());
  EXPECT_TRUE(remoteDataTracks(dispatcher).empty());
}

// ============================================================================
// Concurrent data callback registration
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest,
       ConcurrentDataCallbackRegistrationDoesNotCrash) {
  SubscriptionThreadDispatcher dispatcher;
  constexpr int kThreads = 8;
  constexpr int kIterations = 100;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&dispatcher, t]() {
      for (int i = 0; i < kIterations; ++i) {
        auto id = dispatcher.addOnDataFrameCallback(
            "participant-" + std::to_string(t), "track",
            [](const std::vector<std::uint8_t> &,
               std::optional<std::uint64_t>) {});
        dispatcher.removeOnDataFrameCallback(id);
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  EXPECT_TRUE(dataCallbacks(dispatcher).empty())
      << "All data callbacks should be cleared after concurrent "
         "register/remove";
}

} // namespace livekit
