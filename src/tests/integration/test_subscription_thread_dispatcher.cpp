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

  static auto &audioCallbacks(SubscriptionThreadDispatcher &dispatcher) {
    return dispatcher.audio_callbacks_;
  }
  static auto &videoCallbacks(SubscriptionThreadDispatcher &dispatcher) {
    return dispatcher.video_callbacks_;
  }
  static auto &activeReaders(SubscriptionThreadDispatcher &dispatcher) {
    return dispatcher.active_readers_;
  }
  static int maxActiveReaders() {
    return SubscriptionThreadDispatcher::kMaxActiveReaders;
  }
};

// ============================================================================
// CallbackKey equality
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyEqualKeysCompareEqual) {
  CallbackKey a{"alice", TrackSource::SOURCE_MICROPHONE};
  CallbackKey b{"alice", TrackSource::SOURCE_MICROPHONE};
  EXPECT_TRUE(a == b);
}

TEST_F(SubscriptionThreadDispatcherTest,
       CallbackKeyDifferentIdentityNotEqual) {
  CallbackKey a{"alice", TrackSource::SOURCE_MICROPHONE};
  CallbackKey b{"bob", TrackSource::SOURCE_MICROPHONE};
  EXPECT_FALSE(a == b);
}

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyDifferentSourceNotEqual) {
  CallbackKey a{"alice", TrackSource::SOURCE_MICROPHONE};
  CallbackKey b{"alice", TrackSource::SOURCE_CAMERA};
  EXPECT_FALSE(a == b);
}

// ============================================================================
// CallbackKeyHash
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest,
       CallbackKeyHashEqualKeysProduceSameHash) {
  CallbackKey a{"alice", TrackSource::SOURCE_MICROPHONE};
  CallbackKey b{"alice", TrackSource::SOURCE_MICROPHONE};
  CallbackKeyHash hasher;
  EXPECT_EQ(hasher(a), hasher(b));
}

TEST_F(SubscriptionThreadDispatcherTest,
       CallbackKeyHashDifferentKeysLikelyDifferentHash) {
  CallbackKeyHash hasher;
  CallbackKey mic{"alice", TrackSource::SOURCE_MICROPHONE};
  CallbackKey cam{"alice", TrackSource::SOURCE_CAMERA};
  CallbackKey bob{"bob", TrackSource::SOURCE_MICROPHONE};

  EXPECT_NE(hasher(mic), hasher(cam));
  EXPECT_NE(hasher(mic), hasher(bob));
}

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyWorksAsUnorderedMapKey) {
  std::unordered_map<CallbackKey, int, CallbackKeyHash> map;

  CallbackKey k1{"alice", TrackSource::SOURCE_MICROPHONE};
  CallbackKey k2{"bob", TrackSource::SOURCE_CAMERA};
  CallbackKey k3{"alice", TrackSource::SOURCE_CAMERA};

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
  CallbackKey a{"", TrackSource::SOURCE_UNKNOWN};
  CallbackKey b{"", TrackSource::SOURCE_UNKNOWN};
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

TEST_F(SubscriptionThreadDispatcherTest, SetVideoCallbackStoresRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                     [](const VideoFrame &, std::int64_t) {});

  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearAudioCallbackRemovesRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                     [](const AudioFrame &) {});
  ASSERT_EQ(audioCallbacks(dispatcher).size(), 1u);

  dispatcher.clearOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE);
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearVideoCallbackRemovesRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                     [](const VideoFrame &, std::int64_t) {});
  ASSERT_EQ(videoCallbacks(dispatcher).size(), 1u);

  dispatcher.clearOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA);
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearNonExistentCallbackIsNoOp) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_NO_THROW(dispatcher.clearOnAudioFrameCallback(
      "nobody", TrackSource::SOURCE_MICROPHONE));
  EXPECT_NO_THROW(
      dispatcher.clearOnVideoFrameCallback("nobody", TrackSource::SOURCE_CAMERA));
}

TEST_F(SubscriptionThreadDispatcherTest, OverwriteAudioCallbackKeepsSingleEntry) {
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

TEST_F(SubscriptionThreadDispatcherTest, OverwriteVideoCallbackKeepsSingleEntry) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                     [](const VideoFrame &, std::int64_t) {});
  dispatcher.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                     [](const VideoFrame &, std::int64_t) {});

  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
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

  dispatcher.clearOnAudioFrameCallback("alice",
                                       TrackSource::SOURCE_MICROPHONE);
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

  dispatcher.clearOnAudioFrameCallback("alice",
                                       TrackSource::SOURCE_MICROPHONE);
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);

  CallbackKey remaining{"alice", TrackSource::SOURCE_SCREENSHARE_AUDIO};
  EXPECT_EQ(audioCallbacks(dispatcher).count(remaining), 1u);
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
        dispatcher.setOnVideoFrameCallback(id, TrackSource::SOURCE_CAMERA,
                                           [](const VideoFrame &,
                                              std::int64_t) {});
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

} // namespace livekit
