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
  void SetUp() override { livekit::initialize(livekit::LogLevel::Info); }

  void TearDown() override { livekit::shutdown(); }
};

TEST_F(RoomCallbackTest, FrameCallbackRegistrationByTrackNameIsAccepted) {
  Room room;

  EXPECT_NO_THROW(room.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {}));
  EXPECT_NO_THROW(room.setOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {}));
  EXPECT_NO_THROW(room.clearOnAudioFrameCallback("alice", "mic-main"));
  EXPECT_NO_THROW(room.clearOnVideoFrameCallback("alice", "cam-main"));
}

TEST_F(RoomCallbackTest, DataCallbackRegistrationReturnsUsableIds) {
  Room room;

  const auto id1 = room.addOnDataFrameCallback("alice", "track-a",
                                               [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
  const auto id2 = room.addOnDataFrameCallback("alice", "track-a",
                                               [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});

  EXPECT_NE(id1, std::numeric_limits<DataFrameCallbackId>::max());
  EXPECT_NE(id2, std::numeric_limits<DataFrameCallbackId>::max());
  EXPECT_NE(id1, id2);

  EXPECT_NO_THROW(room.removeOnDataFrameCallback(id1));
  EXPECT_NO_THROW(room.removeOnDataFrameCallback(id2));
}

TEST_F(RoomCallbackTest, RemovingUnknownDataCallbackIsNoOp) {
  Room room;

  EXPECT_NO_THROW(room.removeOnDataFrameCallback(std::numeric_limits<DataFrameCallbackId>::max()));
}

TEST_F(RoomCallbackTest, DestroyRoomWithRegisteredCallbacksIsSafe) {
  EXPECT_NO_THROW({
    Room room;
    room.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
    room.setOnVideoFrameCallback("bob", "cam-main", [](const VideoFrame&, std::int64_t) {});
    room.addOnDataFrameCallback("carol", "track",
                                [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
  });
}

TEST_F(RoomCallbackTest, DestroyRoomAfterClearingCallbacksIsSafe) {
  EXPECT_NO_THROW({
    Room room;
    room.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
    room.clearOnAudioFrameCallback("alice", "mic-main");

    const auto id = room.addOnDataFrameCallback("alice", "track",
                                                [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
    room.removeOnDataFrameCallback(id);
  });
}

TEST_F(RoomCallbackTest, DefaultConnectionStateIsDisconnected) {
  Room room;
  EXPECT_EQ(room.connectionState(), ConnectionState::Disconnected);
}

TEST_F(RoomCallbackTest, ConnectionStateRemainsDisconnectedWithoutConnect) {
  // Register callbacks, do other operations — state must stay Disconnected.
  Room room;
  room.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
  room.setOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {});
  room.addOnDataFrameCallback("alice", "track", [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
  room.registerTextStreamHandler("topic", [](const std::shared_ptr<TextStreamReader>&, const std::string&) {});
  EXPECT_EQ(room.connectionState(), ConnectionState::Disconnected);
}

TEST_F(RoomCallbackTest, ConnectionStateIsQueryableFromMultipleThreads) {
  Room room;
  constexpr int kThreads = 8;
  constexpr int kIterations = 200;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  std::atomic<int> disconnected_count{0};

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&room, &disconnected_count, kIterations]() {
      for (int i = 0; i < kIterations; ++i) {
        if (room.connectionState() == ConnectionState::Disconnected) {
          disconnected_count.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(disconnected_count.load(), kThreads * kIterations);
}

} // namespace livekit
