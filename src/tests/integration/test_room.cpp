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
    EXPECT_NE(room.localParticipant(), nullptr) << "Local participant should exist after connect";
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

} // namespace livekit::test
