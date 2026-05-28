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

namespace livekit::test {

class RoomTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole); }

  void TearDown() override { livekit::shutdown(); }
};

TEST_F(RoomTest, ConnectWithoutInitialize) {
  // Test fixture initializes by default, do this to emulate lack of initialization
  livekit::shutdown();

  Room room;
  bool result = room.connect("wss://localhost:7880", "test", livekit::RoomOptions());
  EXPECT_FALSE(result) << "Connecting without initializing should return false";
  EXPECT_EQ(room.localParticipant(), nullptr) << "Local participant should be null after failed connect";
  EXPECT_TRUE(room.remoteParticipants().empty()) << "Remote participants should be empty after failed connect";
}

TEST_F(RoomTest, CreateRoom) {
  Room room;
  // Room should be created without issues
  EXPECT_EQ(room.localParticipant(), nullptr) << "Local participant should be null before connect";
}

TEST_F(RoomTest, RoomOptionsDefaults) {
  RoomOptions options;

  EXPECT_TRUE(options.auto_subscribe) << "auto_subscribe should default to true";
  EXPECT_FALSE(options.dynacast) << "dynacast should default to false";
  EXPECT_FALSE(options.rtc_config.has_value()) << "rtc_config should not have a value by default";
  EXPECT_FALSE(options.encryption.has_value()) << "encryption should not have a value by default";
}

TEST_F(RoomTest, RtcConfigDefaults) {
  RtcConfig config;

  EXPECT_EQ(config.ice_transport_type, 0);
  EXPECT_EQ(config.continual_gathering_policy, 0);
  EXPECT_TRUE(config.ice_servers.empty());
}

TEST_F(RoomTest, IceServerConfiguration) {
  IceServer server;
  server.url = "stun:stun.l.google.com:19302";
  server.username = "user";
  server.credential = "pass";

  EXPECT_EQ(server.url, "stun:stun.l.google.com:19302");
  EXPECT_EQ(server.username, "user");
  EXPECT_EQ(server.credential, "pass");
}

TEST_F(RoomTest, RoomWithCustomRtcConfig) {
  RoomOptions options;
  options.auto_subscribe = false;
  options.dynacast = true;

  RtcConfig rtc_config;
  rtc_config.ice_servers.push_back({"stun:stun.l.google.com:19302", "", ""});
  rtc_config.ice_servers.push_back({"turn:turn.example.com:3478", "user", "pass"});

  options.rtc_config = rtc_config;

  EXPECT_FALSE(options.auto_subscribe);
  EXPECT_TRUE(options.dynacast);
  EXPECT_TRUE(options.rtc_config.has_value());
  EXPECT_EQ(options.rtc_config->ice_servers.size(), 2);
}

TEST_F(RoomTest, RemoteParticipantsEmptyBeforeConnect) {
  Room room;
  auto participants = room.remoteParticipants();
  EXPECT_TRUE(participants.empty()) << "Remote participants should be empty before connect";
}

TEST_F(RoomTest, RemoteParticipantLookupBeforeConnect) {
  Room room;
  auto participant = room.remoteParticipant("nonexistent");
  EXPECT_EQ(participant, nullptr) << "Looking up participant before connect should return nullptr";
}

} // namespace livekit::test
