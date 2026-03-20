/*
 * Copyright 2025 LiveKit
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

/// @file test_room_callbacks.cpp
/// @brief Unit tests for Room frame callback registration and internals.

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>

namespace livekit {

class RoomCallbackTest : public ::testing::Test {
protected:
  void SetUp() override {
    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);
  }

  void TearDown() override { livekit::shutdown(); }

  using CallbackKey = Room::CallbackKey;
  using CallbackKeyHash = Room::CallbackKeyHash;

  static auto &audioCallbacks(Room &room) { return room.audio_callbacks_; }
  static auto &videoCallbacks(Room &room) { return room.video_callbacks_; }
  static auto &activeReaders(Room &room) { return room.active_readers_; }
  static int maxActiveReaders() { return Room::kMaxActiveReaders; }
};

// ============================================================================
// CallbackKey equality
// ============================================================================

TEST_F(RoomCallbackTest, CallbackKeyEqualKeysCompareEqual) {
  CallbackKey a{"alice", TrackSource::SOURCE_MICROPHONE};
  CallbackKey b{"alice", TrackSource::SOURCE_MICROPHONE};
  EXPECT_TRUE(a == b);
}

TEST_F(RoomCallbackTest, CallbackKeyDifferentIdentityNotEqual) {
  CallbackKey a{"alice", TrackSource::SOURCE_MICROPHONE};
  CallbackKey b{"bob", TrackSource::SOURCE_MICROPHONE};
  EXPECT_FALSE(a == b);
}

TEST_F(RoomCallbackTest, CallbackKeyDifferentSourceNotEqual) {
  CallbackKey a{"alice", TrackSource::SOURCE_MICROPHONE};
  CallbackKey b{"alice", TrackSource::SOURCE_CAMERA};
  EXPECT_FALSE(a == b);
}

// ============================================================================
// CallbackKeyHash
// ============================================================================

TEST_F(RoomCallbackTest, CallbackKeyHashEqualKeysProduceSameHash) {
  CallbackKey a{"alice", TrackSource::SOURCE_MICROPHONE};
  CallbackKey b{"alice", TrackSource::SOURCE_MICROPHONE};
  CallbackKeyHash hasher;
  EXPECT_EQ(hasher(a), hasher(b));
}

TEST_F(RoomCallbackTest, CallbackKeyHashDifferentKeysLikelyDifferentHash) {
  CallbackKeyHash hasher;
  CallbackKey mic{"alice", TrackSource::SOURCE_MICROPHONE};
  CallbackKey cam{"alice", TrackSource::SOURCE_CAMERA};
  CallbackKey bob{"bob", TrackSource::SOURCE_MICROPHONE};

  EXPECT_NE(hasher(mic), hasher(cam));
  EXPECT_NE(hasher(mic), hasher(bob));
}

TEST_F(RoomCallbackTest, CallbackKeyWorksAsUnorderedMapKey) {
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

TEST_F(RoomCallbackTest, CallbackKeyEmptyIdentityWorks) {
  CallbackKey a{"", TrackSource::SOURCE_UNKNOWN};
  CallbackKey b{"", TrackSource::SOURCE_UNKNOWN};
  CallbackKeyHash hasher;
  EXPECT_TRUE(a == b);
  EXPECT_EQ(hasher(a), hasher(b));
}

// ============================================================================
// kMaxActiveReaders
// ============================================================================

TEST_F(RoomCallbackTest, MaxActiveReadersIs20) {
  EXPECT_EQ(maxActiveReaders(), 20);
}

// ============================================================================
// Registration and clearing (pre-connection, no server needed)
// ============================================================================

TEST_F(RoomCallbackTest, SetAudioCallbackStoresRegistration) {
  Room room;
  room.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                               [](const AudioFrame &) {});

  EXPECT_EQ(audioCallbacks(room).size(), 1u);
}

TEST_F(RoomCallbackTest, SetVideoCallbackStoresRegistration) {
  Room room;
  room.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                               [](const VideoFrame &, std::int64_t) {});

  EXPECT_EQ(videoCallbacks(room).size(), 1u);
}

TEST_F(RoomCallbackTest, ClearAudioCallbackRemovesRegistration) {
  Room room;
  room.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                               [](const AudioFrame &) {});
  ASSERT_EQ(audioCallbacks(room).size(), 1u);

  room.clearOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE);
  EXPECT_EQ(audioCallbacks(room).size(), 0u);
}

TEST_F(RoomCallbackTest, ClearVideoCallbackRemovesRegistration) {
  Room room;
  room.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                               [](const VideoFrame &, std::int64_t) {});
  ASSERT_EQ(videoCallbacks(room).size(), 1u);

  room.clearOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA);
  EXPECT_EQ(videoCallbacks(room).size(), 0u);
}

TEST_F(RoomCallbackTest, ClearNonExistentCallbackIsNoOp) {
  Room room;
  EXPECT_NO_THROW(
      room.clearOnAudioFrameCallback("nobody", TrackSource::SOURCE_MICROPHONE));
  EXPECT_NO_THROW(
      room.clearOnVideoFrameCallback("nobody", TrackSource::SOURCE_CAMERA));
}

TEST_F(RoomCallbackTest, OverwriteAudioCallbackKeepsSingleEntry) {
  Room room;
  std::atomic<int> counter1{0};
  std::atomic<int> counter2{0};

  room.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                               [&counter1](const AudioFrame &) { counter1++; });
  room.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                               [&counter2](const AudioFrame &) { counter2++; });

  EXPECT_EQ(audioCallbacks(room).size(), 1u)
      << "Re-registering with the same key should overwrite, not add";
}

TEST_F(RoomCallbackTest, OverwriteVideoCallbackKeepsSingleEntry) {
  Room room;
  room.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                               [](const VideoFrame &, std::int64_t) {});
  room.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                               [](const VideoFrame &, std::int64_t) {});

  EXPECT_EQ(videoCallbacks(room).size(), 1u);
}

TEST_F(RoomCallbackTest, MultipleDistinctCallbacksAreIndependent) {
  Room room;
  room.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                               [](const AudioFrame &) {});
  room.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                               [](const VideoFrame &, std::int64_t) {});
  room.setOnAudioFrameCallback("bob", TrackSource::SOURCE_MICROPHONE,
                               [](const AudioFrame &) {});
  room.setOnVideoFrameCallback("bob", TrackSource::SOURCE_CAMERA,
                               [](const VideoFrame &, std::int64_t) {});

  EXPECT_EQ(audioCallbacks(room).size(), 2u);
  EXPECT_EQ(videoCallbacks(room).size(), 2u);

  room.clearOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE);
  EXPECT_EQ(audioCallbacks(room).size(), 1u);
  EXPECT_EQ(videoCallbacks(room).size(), 2u);
}

TEST_F(RoomCallbackTest, ClearingOneSourceDoesNotAffectOther) {
  Room room;
  room.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                               [](const AudioFrame &) {});
  room.setOnAudioFrameCallback("alice", TrackSource::SOURCE_SCREENSHARE_AUDIO,
                               [](const AudioFrame &) {});
  ASSERT_EQ(audioCallbacks(room).size(), 2u);

  room.clearOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE);
  EXPECT_EQ(audioCallbacks(room).size(), 1u);

  CallbackKey remaining{"alice", TrackSource::SOURCE_SCREENSHARE_AUDIO};
  EXPECT_EQ(audioCallbacks(room).count(remaining), 1u);
}

// ============================================================================
// Active readers state (no real streams, just map state)
// ============================================================================

TEST_F(RoomCallbackTest, NoActiveReadersInitially) {
  Room room;
  EXPECT_TRUE(activeReaders(room).empty());
}

TEST_F(RoomCallbackTest, ActiveReadersEmptyAfterCallbackRegistration) {
  Room room;
  room.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                               [](const AudioFrame &) {});
  EXPECT_TRUE(activeReaders(room).empty())
      << "Registering a callback without a subscribed track should not spawn "
         "readers";
}

// ============================================================================
// Destruction safety
// ============================================================================

TEST_F(RoomCallbackTest, DestroyRoomWithRegisteredCallbacksIsSafe) {
  EXPECT_NO_THROW({
    Room room;
    room.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                 [](const AudioFrame &) {});
    room.setOnVideoFrameCallback("bob", TrackSource::SOURCE_CAMERA,
                                 [](const VideoFrame &, std::int64_t) {});
  });
}

TEST_F(RoomCallbackTest, DestroyRoomAfterClearingCallbacksIsSafe) {
  EXPECT_NO_THROW({
    Room room;
    room.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                 [](const AudioFrame &) {});
    room.clearOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE);
  });
}

// ============================================================================
// Thread-safety of registration/clearing
// ============================================================================

TEST_F(RoomCallbackTest, ConcurrentRegistrationDoesNotCrash) {
  Room room;
  constexpr int kThreads = 8;
  constexpr int kIterations = 100;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&room, t]() {
      for (int i = 0; i < kIterations; ++i) {
        std::string id = "participant-" + std::to_string(t);
        room.setOnAudioFrameCallback(id, TrackSource::SOURCE_MICROPHONE,
                                     [](const AudioFrame &) {});
        room.clearOnAudioFrameCallback(id, TrackSource::SOURCE_MICROPHONE);
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  EXPECT_TRUE(audioCallbacks(room).empty())
      << "All callbacks should be cleared after concurrent register/clear";
}

TEST_F(RoomCallbackTest, ConcurrentMixedAudioVideoRegistration) {
  Room room;
  constexpr int kThreads = 4;
  constexpr int kIterations = 50;

  std::vector<std::thread> threads;

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&room, t]() {
      std::string id = "p-" + std::to_string(t);
      for (int i = 0; i < kIterations; ++i) {
        room.setOnAudioFrameCallback(id, TrackSource::SOURCE_MICROPHONE,
                                     [](const AudioFrame &) {});
        room.setOnVideoFrameCallback(id, TrackSource::SOURCE_CAMERA,
                                     [](const VideoFrame &, std::int64_t) {});
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  EXPECT_EQ(audioCallbacks(room).size(), static_cast<size_t>(kThreads));
  EXPECT_EQ(videoCallbacks(room).size(), static_cast<size_t>(kThreads));
}

// ============================================================================
// Bulk registration
// ============================================================================

TEST_F(RoomCallbackTest, ManyDistinctCallbacksCanBeRegistered) {
  Room room;
  constexpr int kCount = 50;

  for (int i = 0; i < kCount; ++i) {
    room.setOnAudioFrameCallback("participant-" + std::to_string(i),
                                 TrackSource::SOURCE_MICROPHONE,
                                 [](const AudioFrame &) {});
  }

  EXPECT_EQ(audioCallbacks(room).size(), static_cast<size_t>(kCount));

  for (int i = 0; i < kCount; ++i) {
    room.clearOnAudioFrameCallback("participant-" + std::to_string(i),
                                   TrackSource::SOURCE_MICROPHONE);
  }

  EXPECT_EQ(audioCallbacks(room).size(), 0u);
}

} // namespace livekit
