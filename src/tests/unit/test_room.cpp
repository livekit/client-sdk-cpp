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

#include <chrono>
#include <iostream>
#include <string>

#include "../common/test_common.h"
#include "room_proto_converter.h"

namespace livekit::test {

namespace {

constexpr const char* kRustRetryLogFilter = "livekit::rtc_engine=warn,livekit_ffi::server::room=error";

// Configure RUST_LOG during static initialization, before any test can initialize
// the Rust FFI logger (which reads env vars once at construction time).
const ScopedEnv kRustLogEnv("RUST_LOG", kRustRetryLogFilter);

} // namespace

class RoomTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogLevel::Info); }

  void TearDown() override { livekit::shutdown(); }
};

TEST_F(RoomTest, ConnectWithoutInitialize) {
  // Test fixture initializes by default, do this to emulate lack of initialization
  livekit::shutdown();

  Room room;

  // Default room options okay here, will return before FFI layer since not initialized
  bool result = room.connect("wss://localhost:7880", "test", livekit::RoomOptions());
  EXPECT_FALSE(result) << "Connecting without initializing should return false";
  EXPECT_TRUE(room.localParticipant().expired()) << "Local participant should be empty after failed connect";
  EXPECT_TRUE(room.remoteParticipants().empty()) << "Remote participants should be empty after failed connect";
}

TEST_F(RoomTest, CreateRoom) {
  Room room;
  // Room should be created without issues
  EXPECT_TRUE(room.localParticipant().expired()) << "Local participant should be empty before connect";
}

TEST_F(RoomTest, RoomOptionsDefaults) {
  RoomOptions options;

  EXPECT_TRUE(options.auto_subscribe) << "auto_subscribe should default to true";
  EXPECT_FALSE(options.adaptive_stream.has_value()) << "adaptive_stream should defer to Rust default";
  EXPECT_FALSE(options.dynacast) << "dynacast should default to false";
  EXPECT_FALSE(options.encryption.has_value()) << "encryption should not have a value by default";
  EXPECT_FALSE(options.rtc_config.has_value()) << "rtc_config should not have a value by default";
  EXPECT_FALSE(options.join_retries.has_value()) << "join_retries should defer to Rust default";
  EXPECT_TRUE(options.single_peer_connection) << "single_peer_connection should default to true";
  EXPECT_FALSE(options.connect_timeout.has_value()) << "connect_timeout should defer to Rust default";
}

TEST_F(RoomTest, RoomOptionsToProtoSerializesDefaults) {
  const proto::RoomOptions proto_options = toProto(RoomOptions{});

  EXPECT_TRUE(proto_options.has_auto_subscribe());
  EXPECT_TRUE(proto_options.auto_subscribe());
  EXPECT_FALSE(proto_options.has_adaptive_stream());
  EXPECT_TRUE(proto_options.has_dynacast());
  EXPECT_FALSE(proto_options.dynacast());
  EXPECT_FALSE(proto_options.has_encryption());
  EXPECT_FALSE(proto_options.has_rtc_config());
  EXPECT_FALSE(proto_options.has_join_retries());
  EXPECT_TRUE(proto_options.has_single_peer_connection());
  EXPECT_TRUE(proto_options.single_peer_connection());
  EXPECT_FALSE(proto_options.has_connect_timeout_ms());
}

TEST_F(RoomTest, RoomOptionsProtoConverter) {
  RoomOptions options;
  options.auto_subscribe = false;
  options.adaptive_stream = true;
  options.dynacast = true;
  E2EEOptions encryption;
  encryption.key_provider_options.shared_key = std::vector<std::uint8_t>{'s', 'e', 'c', 'r', 'e', 't'};
  options.encryption = encryption;
  RtcConfig rtc_config;
  rtc_config.ice_transport_type = proto::TRANSPORT_ALL;
  rtc_config.continual_gathering_policy = proto::GATHER_CONTINUALLY;
  rtc_config.ice_servers.push_back({"stun:stun.l.google.com:19302", "", ""});
  rtc_config.ice_servers.push_back({"turn:turn.example.com:3478", "user", "pass"});
  options.rtc_config = rtc_config;
  options.join_retries = 8;
  options.single_peer_connection = false;
  options.connect_timeout = std::chrono::milliseconds(750);

  const proto::RoomOptions proto_options = toProto(options);

  EXPECT_TRUE(proto_options.has_auto_subscribe());
  EXPECT_FALSE(proto_options.auto_subscribe());
  EXPECT_TRUE(proto_options.has_adaptive_stream());
  EXPECT_TRUE(proto_options.adaptive_stream());
  EXPECT_TRUE(proto_options.has_dynacast());
  EXPECT_TRUE(proto_options.dynacast());
  ASSERT_TRUE(proto_options.has_encryption());
  EXPECT_EQ(proto_options.encryption().encryption_type(),
            static_cast<proto::EncryptionType>(encryption.encryption_type));
  ASSERT_TRUE(proto_options.encryption().has_key_provider_options());
  EXPECT_EQ(proto_options.encryption().key_provider_options().shared_key(), "secret");
  ASSERT_TRUE(proto_options.has_rtc_config());
  EXPECT_EQ(proto_options.rtc_config().ice_transport_type(), proto::TRANSPORT_ALL);
  EXPECT_EQ(proto_options.rtc_config().continual_gathering_policy(), proto::GATHER_CONTINUALLY);
  ASSERT_EQ(proto_options.rtc_config().ice_servers_size(), 2);
  EXPECT_EQ(proto_options.rtc_config().ice_servers(0).urls(0), "stun:stun.l.google.com:19302");
  EXPECT_EQ(proto_options.rtc_config().ice_servers(1).urls(0), "turn:turn.example.com:3478");
  EXPECT_EQ(proto_options.rtc_config().ice_servers(1).username(), "user");
  EXPECT_EQ(proto_options.rtc_config().ice_servers(1).password(), "pass");
  EXPECT_TRUE(proto_options.has_join_retries());
  EXPECT_EQ(proto_options.join_retries(), 8U);
  EXPECT_TRUE(proto_options.has_single_peer_connection());
  EXPECT_FALSE(proto_options.single_peer_connection());
  EXPECT_TRUE(proto_options.has_connect_timeout_ms());
  EXPECT_EQ(proto_options.connect_timeout_ms(), 750U);
}

// This test validates the join retries behavior when connecting to an invalid URL
// It sets the RUST_LOG env variable to capture the retry logs
TEST_F(RoomTest, RoomOptionsJoinRetries) {
  constexpr std::uint32_t kJoinRetries = 10;

  Room room;
  RoomOptions options;
  options.join_retries = kJoinRetries;

  testing::internal::CaptureStderr();
  const bool result = room.connect("not-a-livekit-url", "test-token", options);
  const std::string stderr_output = testing::internal::GetCapturedStderr();

  EXPECT_FALSE(result) << "Connecting with an invalid URL should fail";
  EXPECT_TRUE(room.localParticipant().expired()) << "Local participant should be empty after failed connect";
  EXPECT_TRUE(room.remoteParticipants().empty()) << "Remote participants should be empty after failed connect";

  // Do the below that way we can print stderr only once if there was a string change to the output
  const bool has_failure = HasFailure();

  EXPECT_NE(stderr_output.find("Room::connect failed:"), std::string::npos);
  EXPECT_EQ(countOccurrences(stderr_output, "retrying..."), kJoinRetries);
  EXPECT_NE(stderr_output.find("retrying... (10/10)"), std::string::npos);

  if (!has_failure && HasFailure()) {
    std::cerr << "### One or more checks failed due to log format changing. Captured stderr output below ###\n";
    std::cerr << stderr_output << "\n";
  }
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
  EXPECT_TRUE(room.remoteParticipant("nonexistent").expired())
      << "Looking up participant before connect should return an empty handle";
}

} // namespace livekit::test
