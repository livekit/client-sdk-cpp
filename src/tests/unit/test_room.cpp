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

#include <atomic>
#include <chrono>
#include <future>
#include <string>

#include "../common/ffi_utils.h"
#include "ffi.pb.h"
#include "ffi_client.h"
#include "room_proto_converter.h"

namespace livekit {

struct RoomTestAccess {
  static void installConnectedListener(Room& room, std::atomic<int>& callback_count) {
    const auto listener_id = FfiClient::instance().addListener([&room, &callback_count](const proto::FfiEvent& event) {
      callback_count.fetch_add(1, std::memory_order_relaxed);
      room.onEvent(event);
    });

    const std::scoped_lock<std::mutex> guard(room.lock_);
    room.connection_state_ = ConnectionState::Connected;
    room.room_handle_ = std::make_shared<FfiHandle>();
    room.listener_id_ = listener_id;
    room.teardown_started_ = false;
  }

  static bool hasRoomHandle(const Room& room) {
    const std::scoped_lock<std::mutex> guard(room.lock_);
    return static_cast<bool>(room.room_handle_);
  }

  static int listenerId(const Room& room) {
    const std::scoped_lock<std::mutex> guard(room.lock_);
    return room.listener_id_;
  }
};

} // namespace livekit

namespace livekit::test {

class RoomTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogLevel::Info); }

  void TearDown() override { livekit::shutdown(); }
};

namespace {

class UnitDisconnectTrackingDelegate : public RoomDelegate {
public:
  void onDisconnected(Room&, const DisconnectedEvent& event) override {
    ++count;
    reason = event.reason;
  }

  int count = 0;
  DisconnectReason reason = DisconnectReason::Unknown;
};

} // namespace

TEST_F(RoomTest, ServerDisconnectTearsDownRoomAndRemovesListener) {
  Room room;
  UnitDisconnectTrackingDelegate delegate;
  std::atomic<int> listener_calls{0};
  room.setDelegate(&delegate);
  RoomTestAccess::installConnectedListener(room, listener_calls);

  proto::FfiEvent event;
  auto* room_event = event.mutable_room_event();
  room_event->set_room_handle(0);
  room_event->mutable_disconnected()->set_reason(proto::ROOM_DELETED);

  emitFfiEvent(event);

  EXPECT_EQ(listener_calls.load(std::memory_order_relaxed), 1);
  EXPECT_EQ(room.connectionState(), ConnectionState::Disconnected);
  EXPECT_FALSE(RoomTestAccess::hasRoomHandle(room));
  EXPECT_EQ(RoomTestAccess::listenerId(room), 0);
  EXPECT_EQ(delegate.count, 1);
  EXPECT_EQ(delegate.reason, DisconnectReason::RoomDeleted);

  emitFfiEvent(event);
  EXPECT_EQ(listener_calls.load(std::memory_order_relaxed), 1) << "server disconnect must unregister the Room listener";
  EXPECT_EQ(delegate.count, 1) << "server disconnect must notify the delegate exactly once";
  EXPECT_FALSE(room.disconnect()) << "disconnect after server teardown must be a no-op";
}

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

TEST_F(RoomTest, LiteralTokenSourceEmptyCredentialsFails) {
  auto source = LiteralTokenSource::create("wss://localhost:7880", "");
  const auto result = source->fetch().get();
  EXPECT_FALSE(result) << "Fetching empty credentials should fail before connect";
}

TEST_F(RoomTest, ConnectWithLiteralTokenSourceWithoutInitialize) {
  // Test fixture initializes by default, do this to emulate lack of initialization
  livekit::shutdown();

  Room room;
  auto source = LiteralTokenSource::create("wss://localhost:7880", "jwt-token");
  const auto details = source->fetch().get();
  ASSERT_TRUE(details);

  const bool result = room.connect(details.value().server_url, details.value().participant_token, RoomOptions());
  EXPECT_FALSE(result) << "Connecting without initializing should return false";
}

TEST_F(RoomTest, CustomTokenSourceThrowFails) {
  auto source = CustomTokenSource::create(
      [](const TokenRequestOptions&) -> std::future<Result<TokenSourceResponse, TokenSourceError>> {
        std::promise<Result<TokenSourceResponse, TokenSourceError>> promise;
        promise.set_exception(std::make_exception_ptr(std::runtime_error("token fetch failed")));
        return promise.get_future();
      });

  EXPECT_THROW((void)source->fetch(TokenRequestOptions{}).get(), std::runtime_error);
}

TEST_F(RoomTest, CustomTokenSourceErrorFails) {
  auto source = CustomTokenSource::create(
      [](const TokenRequestOptions&) -> std::future<Result<TokenSourceResponse, TokenSourceError>> {
        std::promise<Result<TokenSourceResponse, TokenSourceError>> promise;
        promise.set_value(
            Result<TokenSourceResponse, TokenSourceError>::failure(TokenSourceError{"backend unavailable"}));
        return promise.get_future();
      });

  const auto result = source->fetch(TokenRequestOptions{}).get();
  EXPECT_FALSE(result) << "Fetching when token source returns error should fail";
}

TEST_F(RoomTest, ConnectWithLiteralProvider) {
  livekit::shutdown();

  Room room;
  int fetch_count = 0;
  auto source =
      LiteralTokenSource::create([&fetch_count]() -> std::future<Result<TokenSourceResponse, TokenSourceError>> {
        ++fetch_count;
        TokenSourceResponse details;
        details.server_url = "wss://localhost:7880";
        details.participant_token = "fetched-token";
        std::promise<Result<TokenSourceResponse, TokenSourceError>> promise;
        promise.set_value(Result<TokenSourceResponse, TokenSourceError>::success(details));
        return promise.get_future();
      });

  const auto details = source->fetch().get();
  ASSERT_TRUE(details);
  EXPECT_EQ(fetch_count, 1) << "Token source should be invoked once";

  const bool result = room.connect(details.value().server_url, details.value().participant_token, RoomOptions());
  EXPECT_FALSE(result) << "Connecting without initializing should return false";
}

TEST(RoomOptionsProtoTest, TokenRefreshedFromProto) {
  proto::TokenRefreshed refreshed;
  refreshed.set_token("refreshed-jwt");

  const livekit::TokenRefreshedEvent event = livekit::fromProto(refreshed);
  EXPECT_EQ(event.token, "refreshed-jwt");
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

TEST(RoomOptionsProtoTest, ConnectRequestSerializesRetryOptions) {
  RoomOptions options;
  options.join_retries = 8;
  options.connect_timeout = std::chrono::milliseconds(750);

  proto::FfiRequest request;
  auto* connect = request.mutable_connect();
  connect->set_url("ws://localhost:7880");
  connect->set_token("test-token");
  connect->mutable_options()->CopyFrom(toProto(options));

  ASSERT_TRUE(connect->options().has_join_retries());
  EXPECT_EQ(connect->options().join_retries(), 8U);
  ASSERT_TRUE(connect->options().has_connect_timeout_ms());
  EXPECT_EQ(connect->options().connect_timeout_ms(), 750U);

  ASSERT_TRUE(request.IsInitialized()) << request.InitializationErrorString();

  std::string serialized;
  ASSERT_TRUE(request.SerializeToString(&serialized));
  EXPECT_FALSE(serialized.empty());

  proto::FfiRequest decoded;
  ASSERT_TRUE(decoded.ParseFromString(serialized));
  EXPECT_EQ(decoded.connect().options().join_retries(), 8U);
  EXPECT_EQ(decoded.connect().options().connect_timeout_ms(), 750U);
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
