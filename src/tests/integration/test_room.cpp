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

namespace livekit {
namespace test {

class RoomTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogSink::kConsole); }

  void TearDown() override { livekit::shutdown(); }
};

TEST_F(RoomTest, CreateRoom) {
  Room room;
  // Room should be created without issues
  EXPECT_EQ(room.localParticipant(), nullptr)
      << "Local participant should be null before connect";
}

TEST_F(RoomTest, RoomOptionsDefaults) {
  RoomOptions options;

  EXPECT_TRUE(options.auto_subscribe)
      << "auto_subscribe should default to true";
  EXPECT_FALSE(options.dynacast) << "dynacast should default to false";
  EXPECT_FALSE(options.rtc_config.has_value())
      << "rtc_config should not have a value by default";
  EXPECT_FALSE(options.encryption.has_value())
      << "encryption should not have a value by default";
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
  rtc_config.ice_servers.push_back(
      {"turn:turn.example.com:3478", "user", "pass"});

  options.rtc_config = rtc_config;

  EXPECT_FALSE(options.auto_subscribe);
  EXPECT_TRUE(options.dynacast);
  EXPECT_TRUE(options.rtc_config.has_value());
  EXPECT_EQ(options.rtc_config->ice_servers.size(), 2);
}

TEST_F(RoomTest, RemoteParticipantsEmptyBeforeConnect) {
  Room room;
  auto participants = room.remoteParticipants();
  EXPECT_TRUE(participants.empty())
      << "Remote participants should be empty before connect";
}

TEST_F(RoomTest, RemoteParticipantLookupBeforeConnect) {
  Room room;
  auto participant = room.remoteParticipant("nonexistent");
  EXPECT_EQ(participant, nullptr)
      << "Looking up participant before connect should return nullptr";
}

// Server-dependent tests - require LIVEKIT_URL and LIVEKIT_TOKEN env vars
class RoomServerTest : public ::testing::Test {
protected:
  void SetUp() override {
    livekit::initialize(livekit::LogSink::kConsole);

    const char *url_env = std::getenv("LIVEKIT_URL");
    const char *token_env = std::getenv("LIVEKIT_CALLER_TOKEN");

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

TEST_F(RoomServerTest, ConnectToServer) {
  if (!server_available_) {
    GTEST_SKIP() << "LIVEKIT_URL and LIVEKIT_TOKEN not set, skipping server "
                    "connection test";
  }

  Room room;
  RoomOptions options;

  bool connected = room.Connect(server_url_, token_, options);
  EXPECT_TRUE(connected) << "Should connect to server successfully";

  if (connected) {
    EXPECT_NE(room.localParticipant(), nullptr)
        << "Local participant should exist after connect";
  }
}

TEST_F(RoomServerTest, ConnectWithInvalidToken) {
  if (!server_available_) {
    GTEST_SKIP() << "LIVEKIT_URL not set, skipping invalid token test";
  }

  Room room;
  RoomOptions options;

  bool connected = room.Connect(server_url_, "invalid_token", options);
  EXPECT_FALSE(connected) << "Should fail to connect with invalid token";
}

TEST_F(RoomServerTest, ConnectWithInvalidUrl) {
  Room room;
  RoomOptions options;

  bool connected = room.Connect("wss://invalid.example.com", "token", options);
  EXPECT_FALSE(connected) << "Should fail to connect to invalid URL";
}

} // namespace test
} // namespace livekit
