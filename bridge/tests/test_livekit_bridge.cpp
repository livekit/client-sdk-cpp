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
#include <livekit_bridge/livekit_bridge.h>

#include <livekit/track.h>

#include <stdexcept>

namespace livekit_bridge {
namespace test {

class LiveKitBridgeTest : public ::testing::Test {
protected:
  // No SetUp/TearDown needed -- we test the bridge without initializing
  // the LiveKit SDK, since we only exercise pre-connection behaviour.
};

// ============================================================================
// Initial state
// ============================================================================

TEST_F(LiveKitBridgeTest, InitiallyNotConnected) {
  LiveKitBridge bridge;

  EXPECT_FALSE(bridge.isConnected())
      << "Bridge should not be connected immediately after construction";
}

TEST_F(LiveKitBridgeTest, DisconnectBeforeConnectIsNoOp) {
  LiveKitBridge bridge;

  EXPECT_NO_THROW(bridge.disconnect())
      << "disconnect() on an unconnected bridge should be a safe no-op";

  EXPECT_FALSE(bridge.isConnected());
}

TEST_F(LiveKitBridgeTest, MultipleDisconnectsAreIdempotent) {
  LiveKitBridge bridge;

  EXPECT_NO_THROW({
    bridge.disconnect();
    bridge.disconnect();
    bridge.disconnect();
  }) << "Multiple disconnect() calls should be safe";
}

TEST_F(LiveKitBridgeTest, DestructorOnUnconnectedBridgeIsSafe) {
  // Just verify no crash when the bridge is destroyed without connecting.
  EXPECT_NO_THROW({
    LiveKitBridge bridge;
    // bridge goes out of scope here
  });
}

// ============================================================================
// Track creation before connection
// ============================================================================

TEST_F(LiveKitBridgeTest, CreateAudioTrackBeforeConnectThrows) {
  LiveKitBridge bridge;

  EXPECT_THROW(bridge.createAudioTrack("mic", 48000, 2,
                                       livekit::TrackSource::SOURCE_MICROPHONE),
               std::runtime_error)
      << "createAudioTrack should throw when not connected";
}

TEST_F(LiveKitBridgeTest, CreateVideoTrackBeforeConnectThrows) {
  LiveKitBridge bridge;

  EXPECT_THROW(bridge.createVideoTrack("cam", 1280, 720,
                                       livekit::TrackSource::SOURCE_CAMERA),
               std::runtime_error)
      << "createVideoTrack should throw when not connected";
}

// ============================================================================
// Callback registration (pre-connection, pure map operations)
// ============================================================================

TEST_F(LiveKitBridgeTest, RegisterAndUnregisterAudioCallbackDoesNotCrash) {
  LiveKitBridge bridge;

  EXPECT_NO_THROW({
    bridge.registerOnAudioFrame("remote-participant",
                                livekit::TrackSource::SOURCE_MICROPHONE,
                                [](const livekit::AudioFrame &) {});

    bridge.unregisterOnAudioFrame("remote-participant",
                                  livekit::TrackSource::SOURCE_MICROPHONE);
  }) << "Registering and unregistering an audio callback should be safe "
        "even without a connection";
}

TEST_F(LiveKitBridgeTest, RegisterAndUnregisterVideoCallbackDoesNotCrash) {
  LiveKitBridge bridge;

  EXPECT_NO_THROW({
    bridge.registerOnVideoFrame(
        "remote-participant", livekit::TrackSource::SOURCE_CAMERA,
        [](const livekit::VideoFrame &, std::int64_t) {});

    bridge.unregisterOnVideoFrame("remote-participant",
                                  livekit::TrackSource::SOURCE_CAMERA);
  }) << "Registering and unregistering a video callback should be safe "
        "even without a connection";
}

TEST_F(LiveKitBridgeTest, UnregisterNonExistentCallbackIsNoOp) {
  LiveKitBridge bridge;

  EXPECT_NO_THROW({
    bridge.unregisterOnAudioFrame("nonexistent",
                                  livekit::TrackSource::SOURCE_MICROPHONE);
    bridge.unregisterOnVideoFrame("nonexistent",
                                  livekit::TrackSource::SOURCE_CAMERA);
  }) << "Unregistering a callback that was never registered should be a no-op";
}

TEST_F(LiveKitBridgeTest, MultipleRegistrationsSameKeyOverwrites) {
  LiveKitBridge bridge;

  int call_count = 0;

  // Register a first callback
  bridge.registerOnAudioFrame("alice", livekit::TrackSource::SOURCE_MICROPHONE,
                              [](const livekit::AudioFrame &) {});

  // Register a second callback for the same key -- should overwrite
  bridge.registerOnAudioFrame(
      "alice", livekit::TrackSource::SOURCE_MICROPHONE,
      [&call_count](const livekit::AudioFrame &) { call_count++; });

  // Unregister once should be enough (only one entry per key)
  EXPECT_NO_THROW(bridge.unregisterOnAudioFrame(
      "alice", livekit::TrackSource::SOURCE_MICROPHONE));
}

TEST_F(LiveKitBridgeTest, RegisterCallbacksForMultipleParticipants) {
  LiveKitBridge bridge;

  EXPECT_NO_THROW({
    bridge.registerOnAudioFrame("alice",
                                livekit::TrackSource::SOURCE_MICROPHONE,
                                [](const livekit::AudioFrame &) {});

    bridge.registerOnVideoFrame(
        "bob", livekit::TrackSource::SOURCE_CAMERA,
        [](const livekit::VideoFrame &, std::int64_t) {});

    bridge.registerOnAudioFrame("charlie",
                                livekit::TrackSource::SOURCE_SCREENSHARE_AUDIO,
                                [](const livekit::AudioFrame &) {});
  }) << "Should be able to register callbacks for multiple participants";

  // Cleanup
  bridge.unregisterOnAudioFrame("alice",
                                livekit::TrackSource::SOURCE_MICROPHONE);
  bridge.unregisterOnVideoFrame("bob", livekit::TrackSource::SOURCE_CAMERA);
  bridge.unregisterOnAudioFrame("charlie",
                                livekit::TrackSource::SOURCE_SCREENSHARE_AUDIO);
}

} // namespace test
} // namespace livekit_bridge
