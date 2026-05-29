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

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <memory>
#include <string>
#include <vector>

#include "../common/test_common.h"

namespace livekit::test {

// Server-dependent tests - require LIVEKIT_URL and LIVEKIT_TOKEN_A env vars
class RoomTest : public ::testing::Test {
protected:
  void SetUp() override {
    livekit::initialize(livekit::LogLevel::Info);

    const char* url_env = std::getenv("LIVEKIT_URL");
    const char* token_env = std::getenv("LIVEKIT_TOKEN_A");

    if (url_env && token_env) {
      server_url_ = url_env;
      token_ = token_env;
      server_available_ = true;
    }
  }

  void TearDown() override { livekit::shutdown(); }

  bool server_available_ = false;
  std::string server_url_;
  std::string token_;
};

TEST_F(RoomTest, ConnectToServer) {
  Room room;
  RoomOptions options;

  bool connected = room.connect(server_url_, token_, options);
  EXPECT_TRUE(connected) << "Should connect to server successfully";

  if (connected) {
    EXPECT_FALSE(room.localParticipant().expired()) << "Local participant should exist after connect";
  }
}

TEST_F(RoomTest, ConnectWithInvalidToken) {
  Room room;
  RoomOptions options;

  bool connected = room.connect(server_url_, "invalid_token", options);
  EXPECT_FALSE(connected) << "Should fail to connect with invalid token";
}

TEST_F(RoomTest, ConnectWithInvalidUrl) {
  Room room;
  RoomOptions options;

  bool connected = room.connect("wss://invalid.example.com", "token", options);
  EXPECT_FALSE(connected) << "Should fail to connect to invalid URL";
}

namespace {

class DisconnectTrackingDelegate : public RoomDelegate {
public:
  void onDisconnected(Room&, const DisconnectedEvent& ev) override {
    ++count;
    last_reason = ev.reason;
  }

  std::atomic<int> count{0};
  DisconnectReason last_reason = DisconnectReason::Unknown;
};

} // namespace

// Case: User calls disconnect()
TEST_F(RoomTest, UserDisconnect) {
  Room room;
  DisconnectTrackingDelegate delegate;
  room.setDelegate(&delegate);

  RoomOptions options;
  ASSERT_TRUE(room.connect(server_url_, token_, options)) << "connect failed";
  ASSERT_EQ(room.connectionState(), ConnectionState::Connected);
  ASSERT_NE(room.localParticipant(), nullptr);

  EXPECT_NO_THROW(room.disconnect()) << "disconnect should not throw on a connected room";
  EXPECT_EQ(room.connectionState(), ConnectionState::Disconnected);
  EXPECT_EQ(room.localParticipant(), nullptr) << "local participant should be cleared after disconnect";
  EXPECT_EQ(delegate.count.load(), 1) << "onDisconnected should fire exactly once";
  EXPECT_EQ(delegate.last_reason, DisconnectReason::ClientInitiated);

  // Calling again on an already-disconnected room is a no-op
  EXPECT_NO_THROW(room.disconnect()) << "second disconnect should not throw on an already-disconnected room";
  EXPECT_EQ(delegate.count.load(), 1) << "delegate must not double-fire";
}

// Case: Room goes out of scope while still connected
TEST_F(RoomTest, DestructorDisconnect) {
  std::unique_ptr<Room> room = std::make_unique<Room>();

  DisconnectTrackingDelegate delegate;
  room->setDelegate(&delegate);
  RoomOptions options;
  ASSERT_TRUE(room->connect(server_url_, token_, options));
  ASSERT_EQ(room->connectionState(), ConnectionState::Connected);

  room.reset(); // invokes destructor which calls disconnect()

  EXPECT_EQ(delegate.count.load(), 1) << "destructor should fire onDisconnected exactly once";
  EXPECT_EQ(delegate.last_reason, DisconnectReason::ClientInitiated);
}

// Verifies that participant handles handed out by Room expire once the Room is
// destroyed. Because the accessors return std::weak_ptr and the Room is the
// sole owner of the participant shared_ptrs, a consumer that caches the handles
// can detect teardown via expired()/lock() == nullptr. Requires a second peer
// (TOKEN_B) so the room under test has a remote participant to observe.
class RoomLifecycleTest : public LiveKitTestBase {};

TEST_F(RoomLifecycleTest, ParticipantHandlesExpireOnRoomDestruction) {
  if (!config_.available) {
    throw std::runtime_error("RoomLifecycleTest: test configuration not set up");
  }

  RoomOptions options;
  options.auto_subscribe = true;

  // 1. Connect the room under test plus a peer so a remote participant exists.
  auto room = std::make_unique<Room>();
  ASSERT_TRUE(room->connect(config_.url, config_.token_a, options)) << "Room failed to connect";

  auto peer = std::make_unique<Room>();
  ASSERT_TRUE(peer->connect(config_.url, config_.token_b, options)) << "Peer failed to connect";

  ASSERT_FALSE(peer->localParticipant().expired());
  const std::string peer_identity = lockLocalParticipant(*peer)->identity();
  ASSERT_TRUE(waitForParticipant(room.get(), peer_identity, 10s)) << "Peer not visible to room";

  // 2. Store the local participant handle. Keep the weak_ptr itself - locking
  //    it here would co-own the participant and keep it alive past teardown,
  //    defeating the check.
  std::weak_ptr<LocalParticipant> local_handle = room->localParticipant();
  ASSERT_FALSE(local_handle.expired()) << "Local participant should be live while connected";

  // 3. Store the remote participant handles (again, as weak_ptr).
  std::vector<std::weak_ptr<RemoteParticipant>> remote_handles = room->remoteParticipants();
  ASSERT_FALSE(remote_handles.empty()) << "Expected at least one remote participant";
  for (const auto& handle : remote_handles) {
    EXPECT_FALSE(handle.expired()) << "Remote participant should be live while connected";
  }
  std::weak_ptr<RemoteParticipant> remote_by_identity = room->remoteParticipant(peer_identity);
  ASSERT_FALSE(remote_by_identity.expired());

  // 4. Destroy the room.
  room.reset();

  // 5. Validate every cached handle now reports as expired / null.
  EXPECT_TRUE(local_handle.expired());
  EXPECT_EQ(local_handle.lock(), nullptr);
  EXPECT_TRUE(remote_by_identity.expired());
  EXPECT_EQ(remote_by_identity.lock(), nullptr);
  for (const auto& handle : remote_handles) {
    EXPECT_TRUE(handle.expired());
    EXPECT_EQ(handle.lock(), nullptr);
  }

  peer.reset();
}

} // namespace livekit::test
