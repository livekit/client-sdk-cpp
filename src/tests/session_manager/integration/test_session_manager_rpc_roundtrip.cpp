/*
 * Copyright 2026 LiveKit
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

#include <livekit/rpc_error.h>

namespace livekit {
namespace test {

class SessionManagerRpcRoundtripTest : public SessionManagerTestBase {};

// ---------------------------------------------------------------------------
// Test 1: Basic RPC round-trip through the SessionManager.
//
// Receiver registers an "echo" handler, caller performs an RPC call, and the
// response is verified.
// ---------------------------------------------------------------------------
TEST_F(SessionManagerRpcRoundtripTest, BasicRpcRoundTrip) {
  skipIfNotConfigured();

  std::cout << "\n=== SessionManager RPC Round-Trip Test ===" << std::endl;

  SessionManager caller;
  SessionManager receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  const std::string receiver_identity = "rpc-receiver";

  std::atomic<int> rpc_calls_received{0};
  receiver.registerRpcMethod(
      "echo",
      [&rpc_calls_received](const livekit::RpcInvocationData &data)
          -> std::optional<std::string> {
        rpc_calls_received++;
        size_t checksum = 0;
        for (char c : data.payload) {
          checksum += static_cast<unsigned char>(c);
        }
        return "echo:" + std::to_string(data.payload.size()) + ":" +
               std::to_string(checksum);
      });

  std::cout << "RPC handler registered, performing call..." << std::endl;

  std::string test_payload = "hello from SessionManager";
  std::string response =
      caller.performRpc(receiver_identity, "echo", test_payload, 10.0);

  size_t expected_checksum = 0;
  for (char c : test_payload) {
    expected_checksum += static_cast<unsigned char>(c);
  }
  std::string expected_response =
      "echo:" + std::to_string(test_payload.size()) + ":" +
      std::to_string(expected_checksum);

  std::cout << "Response: " << response << std::endl;
  std::cout << "Expected: " << expected_response << std::endl;

  EXPECT_EQ(response, expected_response);
  EXPECT_EQ(rpc_calls_received.load(), 1);

  receiver.unregisterRpcMethod("echo");
}

// ---------------------------------------------------------------------------
// Test 2: RPC error propagation.
//
// The handler throws an RpcError with a custom code and message. The caller
// should catch the same error code, message, and data.
// ---------------------------------------------------------------------------
TEST_F(SessionManagerRpcRoundtripTest, RpcErrorPropagation) {
  skipIfNotConfigured();

  std::cout << "\n=== SessionManager RPC Error Propagation Test ==="
            << std::endl;

  SessionManager caller;
  SessionManager receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  const std::string receiver_identity = "rpc-receiver";

  receiver.registerRpcMethod(
      "fail-method",
      [](const livekit::RpcInvocationData &) -> std::optional<std::string> {
        throw livekit::RpcError(livekit::RpcError::ErrorCode::APPLICATION_ERROR,
                                "intentional failure", "extra-data");
      });

  std::cout << "Calling method that throws RpcError..." << std::endl;

  try {
    caller.performRpc(receiver_identity, "fail-method", "", 10.0);
    FAIL() << "Expected RpcError to be thrown";
  } catch (const livekit::RpcError &e) {
    std::cout << "Caught RpcError: code=" << e.code() << " message=\""
              << e.message() << "\""
              << " data=\"" << e.data() << "\"" << std::endl;

    EXPECT_EQ(static_cast<livekit::RpcError::ErrorCode>(e.code()),
              livekit::RpcError::ErrorCode::APPLICATION_ERROR);
    EXPECT_EQ(e.message(), "intentional failure");
    EXPECT_EQ(e.data(), "extra-data");
  }

  receiver.unregisterRpcMethod("fail-method");
}

// ---------------------------------------------------------------------------
// Test 3: Calling an unregistered method returns UNSUPPORTED_METHOD.
// ---------------------------------------------------------------------------
TEST_F(SessionManagerRpcRoundtripTest, UnregisteredMethod) {
  skipIfNotConfigured();

  std::cout << "\n=== SessionManager RPC Unsupported Method Test ==="
            << std::endl;

  SessionManager caller;
  SessionManager receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  const std::string receiver_identity = "rpc-receiver";

  std::cout << "Calling nonexistent method..." << std::endl;

  try {
    caller.performRpc(receiver_identity, "nonexistent-method", "", 5.0);
    FAIL() << "Expected RpcError for unsupported method";
  } catch (const livekit::RpcError &e) {
    std::cout << "Caught RpcError: code=" << e.code() << " message=\""
              << e.message() << "\"" << std::endl;

    EXPECT_EQ(static_cast<livekit::RpcError::ErrorCode>(e.code()),
              livekit::RpcError::ErrorCode::UNSUPPORTED_METHOD);
  }
}

// ===========================================================================
// Remote Track Control Tests
// ===========================================================================

class SessionManagerRemoteTrackControlTest : public SessionManagerTestBase {};

// ---------------------------------------------------------------------------
// Test 4: Remote mute of an audio track.
//
// Publisher creates an audio track, enables remote track control. Controller
// requests mute, then unmute.
// ---------------------------------------------------------------------------
TEST_F(SessionManagerRemoteTrackControlTest, RemoteMuteAudioTrack) {
  skipIfNotConfigured();

  std::cout << "\n=== SessionManager Remote Mute Audio Track Test ==="
            << std::endl;

  SessionManager publisher;
  SessionManager controller;

  ASSERT_TRUE(connectPair(controller, publisher));

  const std::string publisher_identity = "rpc-receiver";

  auto audio_track = publisher.createAudioTrack(
      "mic", 48000, 1, livekit::TrackSource::SOURCE_MICROPHONE);
  ASSERT_NE(audio_track, nullptr);

  std::this_thread::sleep_for(2s);

  std::cout << "Requesting mute..." << std::endl;
  EXPECT_NO_THROW(controller.requestRemoteTrackMute(publisher_identity, "mic"));

  std::vector<std::int16_t> silence(480, 0);
  bool pushed_while_muted = audio_track->pushFrame(silence, 480);
  std::cout << "pushFrame while muted: " << pushed_while_muted << std::endl;

  std::cout << "Requesting unmute..." << std::endl;
  EXPECT_NO_THROW(
      controller.requestRemoteTrackUnmute(publisher_identity, "mic"));

  bool pushed_after_unmute = audio_track->pushFrame(silence, 480);
  EXPECT_TRUE(pushed_after_unmute);
  std::cout << "pushFrame after unmute: " << pushed_after_unmute << std::endl;

  audio_track->release();
}

// ---------------------------------------------------------------------------
// Test 5: Remote mute of a video track.
// ---------------------------------------------------------------------------
TEST_F(SessionManagerRemoteTrackControlTest, RemoteMuteVideoTrack) {
  skipIfNotConfigured();

  std::cout << "\n=== SessionManager Remote Mute Video Track Test ==="
            << std::endl;

  SessionManager publisher;
  SessionManager controller;

  ASSERT_TRUE(connectPair(controller, publisher));

  const std::string publisher_identity = "rpc-receiver";

  auto video_track = publisher.createVideoTrack(
      "cam", 320, 240, livekit::TrackSource::SOURCE_CAMERA);
  ASSERT_NE(video_track, nullptr);

  std::this_thread::sleep_for(2s);

  std::cout << "Requesting mute on video track..." << std::endl;
  EXPECT_NO_THROW(controller.requestRemoteTrackMute(publisher_identity, "cam"));

  std::cout << "Requesting unmute on video track..." << std::endl;
  EXPECT_NO_THROW(
      controller.requestRemoteTrackUnmute(publisher_identity, "cam"));

  std::vector<std::uint8_t> frame(320 * 240 * 4, 128);
  bool pushed_after_unmute = video_track->pushFrame(frame);
  EXPECT_TRUE(pushed_after_unmute);
  std::cout << "pushFrame after unmute: " << pushed_after_unmute << std::endl;

  video_track->release();
}

// ---------------------------------------------------------------------------
// Test 7: Remote mute on a nonexistent track returns an error.
// ---------------------------------------------------------------------------
TEST_F(SessionManagerRemoteTrackControlTest, RemoteMuteNonexistentTrack) {
  skipIfNotConfigured();

  std::cout << "\n=== SessionManager Remote Mute Nonexistent Track Test ==="
            << std::endl;

  SessionManager publisher;
  SessionManager controller;

  ASSERT_TRUE(connectPair(controller, publisher));

  const std::string publisher_identity = "rpc-receiver";

  std::this_thread::sleep_for(2s);

  std::cout << "Requesting mute on nonexistent track..." << std::endl;
  try {
    controller.requestRemoteTrackMute(publisher_identity, "no-such-track");
    FAIL() << "Expected RpcError for nonexistent track";
  } catch (const livekit::RpcError &e) {
    std::cout << "Caught RpcError: code=" << e.code() << " message=\""
              << e.message() << "\"" << std::endl;

    EXPECT_EQ(static_cast<livekit::RpcError::ErrorCode>(e.code()),
              livekit::RpcError::ErrorCode::APPLICATION_ERROR);
    EXPECT_NE(e.message().find("track not found"), std::string::npos)
        << "Error message should mention 'track not found'";
  }
}

} // namespace test
} // namespace livekit
