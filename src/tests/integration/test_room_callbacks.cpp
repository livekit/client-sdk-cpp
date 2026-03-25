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
/// @brief Public API tests for Room callback registration.

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <atomic>
#include <limits>
#include <thread>
#include <vector>

namespace livekit {

class RoomCallbackTest : public ::testing::Test {
protected:
  void SetUp() override {
    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);
  }

  void TearDown() override { livekit::shutdown(); }
};

TEST_F(RoomCallbackTest, AudioCallbackRegistrationIsAccepted) {
  Room room;

  EXPECT_NO_THROW(room.setOnAudioFrameCallback(
      "alice", TrackSource::SOURCE_MICROPHONE, [](const AudioFrame &) {}));
}

TEST_F(RoomCallbackTest, VideoCallbackRegistrationIsAccepted) {
  Room room;

  EXPECT_NO_THROW(
      room.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                   [](const VideoFrame &, std::int64_t) {}));
}

TEST_F(RoomCallbackTest, ClearingMissingCallbacksIsNoOp) {
  Room room;

  EXPECT_NO_THROW(
      room.clearOnAudioFrameCallback("nobody", TrackSource::SOURCE_MICROPHONE));
  EXPECT_NO_THROW(
      room.clearOnVideoFrameCallback("nobody", TrackSource::SOURCE_CAMERA));
}

TEST_F(RoomCallbackTest, ReRegisteringSameAudioKeyDoesNotThrow) {
  Room room;
  std::atomic<int> counter1{0};
  std::atomic<int> counter2{0};

  EXPECT_NO_THROW(room.setOnAudioFrameCallback(
      "alice", TrackSource::SOURCE_MICROPHONE,
      [&counter1](const AudioFrame &) { counter1++; }));
  EXPECT_NO_THROW(room.setOnAudioFrameCallback(
      "alice", TrackSource::SOURCE_MICROPHONE,
      [&counter2](const AudioFrame &) { counter2++; }));
}

TEST_F(RoomCallbackTest, ReRegisteringSameVideoKeyDoesNotThrow) {
  Room room;

  EXPECT_NO_THROW(
      room.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                   [](const VideoFrame &, std::int64_t) {}));
  EXPECT_NO_THROW(
      room.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                   [](const VideoFrame &, std::int64_t) {}));
}

TEST_F(RoomCallbackTest, DistinctAudioAndVideoCallbacksCanCoexist) {
  Room room;

  EXPECT_NO_THROW(room.setOnAudioFrameCallback(
      "alice", TrackSource::SOURCE_MICROPHONE, [](const AudioFrame &) {}));
  EXPECT_NO_THROW(
      room.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                   [](const VideoFrame &, std::int64_t) {}));
  EXPECT_NO_THROW(room.setOnAudioFrameCallback(
      "bob", TrackSource::SOURCE_MICROPHONE, [](const AudioFrame &) {}));
  EXPECT_NO_THROW(
      room.setOnVideoFrameCallback("bob", TrackSource::SOURCE_CAMERA,
                                   [](const VideoFrame &, std::int64_t) {}));
}

TEST_F(RoomCallbackTest, SameSourceDifferentTrackNamesAreAccepted) {
  Room room;

  EXPECT_NO_THROW(
      room.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                   [](const VideoFrame &, std::int64_t) {}));
  EXPECT_NO_THROW(
      room.setOnVideoFrameCallback("alice", TrackSource::SOURCE_CAMERA,
                                   [](const VideoFrame &, std::int64_t) {}));
}

TEST_F(RoomCallbackTest, DataCallbackRegistrationReturnsUsableIds) {
  Room room;

  const auto id1 = room.addOnDataFrameCallback(
      "alice", "track-a",
      [](const std::vector<std::uint8_t> &, std::optional<std::uint64_t>) {});
  const auto id2 = room.addOnDataFrameCallback(
      "alice", "track-a",
      [](const std::vector<std::uint8_t> &, std::optional<std::uint64_t>) {});

  EXPECT_NE(id1, std::numeric_limits<DataFrameCallbackId>::max());
  EXPECT_NE(id2, std::numeric_limits<DataFrameCallbackId>::max());
  EXPECT_NE(id1, id2);

  EXPECT_NO_THROW(room.removeOnDataFrameCallback(id1));
  EXPECT_NO_THROW(room.removeOnDataFrameCallback(id2));
}

TEST_F(RoomCallbackTest, RemovingUnknownDataCallbackIsNoOp) {
  Room room;

  EXPECT_NO_THROW(room.removeOnDataFrameCallback(
      std::numeric_limits<DataFrameCallbackId>::max()));
}

TEST_F(RoomCallbackTest, DestroyRoomWithRegisteredCallbacksIsSafe) {
  EXPECT_NO_THROW({
    Room room;
    room.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                 [](const AudioFrame &) {});
    room.setOnVideoFrameCallback("bob", TrackSource::SOURCE_CAMERA,
                                 [](const VideoFrame &, std::int64_t) {});
    room.addOnDataFrameCallback(
        "carol", "track",
        [](const std::vector<std::uint8_t> &, std::optional<std::uint64_t>) {});
  });
}

TEST_F(RoomCallbackTest, DestroyRoomAfterClearingCallbacksIsSafe) {
  EXPECT_NO_THROW({
    Room room;
    room.setOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE,
                                 [](const AudioFrame &) {});
    room.clearOnAudioFrameCallback("alice", TrackSource::SOURCE_MICROPHONE);

    const auto id = room.addOnDataFrameCallback(
        "alice", "track",
        [](const std::vector<std::uint8_t> &, std::optional<std::uint64_t>) {});
    room.removeOnDataFrameCallback(id);
  });
}

TEST_F(RoomCallbackTest, ConcurrentRegistrationDoesNotCrash) {
  Room room;
  constexpr int kThreads = 8;
  constexpr int kIterations = 100;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&room, t]() {
      for (int i = 0; i < kIterations; ++i) {
        const std::string id = "participant-" + std::to_string(t);
        room.setOnAudioFrameCallback(id, TrackSource::SOURCE_MICROPHONE,
                                     [](const AudioFrame &) {});
        room.clearOnAudioFrameCallback(id, TrackSource::SOURCE_MICROPHONE);
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  SUCCEED();
}

TEST_F(RoomCallbackTest, ConcurrentMixedRegistrationDoesNotCrash) {
  Room room;
  constexpr int kThreads = 4;
  constexpr int kIterations = 50;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&room, t]() {
      const std::string id = "p-" + std::to_string(t);
      for (int i = 0; i < kIterations; ++i) {
        room.setOnAudioFrameCallback(id, TrackSource::SOURCE_MICROPHONE,
                                     [](const AudioFrame &) {});
        room.setOnVideoFrameCallback(id, TrackSource::SOURCE_CAMERA,
                                     [](const VideoFrame &, std::int64_t) {});
        const auto data_id =
            room.addOnDataFrameCallback(id, "track",
                                        [](const std::vector<std::uint8_t> &,
                                           std::optional<std::uint64_t>) {});
        room.removeOnDataFrameCallback(data_id);
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  SUCCEED();
}

TEST_F(RoomCallbackTest, ManyDistinctAudioCallbacksCanBeRegisteredAndCleared) {
  Room room;
  constexpr int kCount = 50;

  for (int i = 0; i < kCount; ++i) {
    EXPECT_NO_THROW(room.setOnAudioFrameCallback(
        "participant-" + std::to_string(i), TrackSource::SOURCE_MICROPHONE,
        [](const AudioFrame &) {}));
  }

  for (int i = 0; i < kCount; ++i) {
    EXPECT_NO_THROW(room.clearOnAudioFrameCallback(
        "participant-" + std::to_string(i), TrackSource::SOURCE_MICROPHONE));
  }
}

} // namespace livekit
