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

/// @file test_session_manager.cpp
/// @brief Unit tests for SessionManager.

#include <gtest/gtest.h>
#include <livekit/session_manager/session_manager.h>

#include <livekit/track.h>

#include <stdexcept>

namespace session_manager {
namespace test {

class SessionManagerTest : public ::testing::Test {
protected:
  // No SetUp/TearDown needed -- we test the SessionManager without initializing
  // the LiveKit SDK, since we only exercise pre-connection behaviour.
};

// ============================================================================
// Initial state
// ============================================================================

TEST_F(SessionManagerTest, InitiallyNotConnected) {
  SessionManager session_manager;

  EXPECT_FALSE(session_manager.isConnected())
      << "SessionManager should not be connected immediately after "
         "construction";
}

TEST_F(SessionManagerTest, DisconnectBeforeConnectIsNoOp) {
  SessionManager session_manager;

  EXPECT_NO_THROW(session_manager.disconnect())
      << "disconnect() on an unconnected SessionManager should be a safe no-op";

  EXPECT_FALSE(session_manager.isConnected());
}

TEST_F(SessionManagerTest, MultipleDisconnectsAreIdempotent) {
  SessionManager session_manager;

  EXPECT_NO_THROW({
    session_manager.disconnect();
    session_manager.disconnect();
  }) << "Multiple disconnect() calls should be safe";
}

TEST_F(SessionManagerTest, DestructorOnUnconnectedSessionManagerIsSafe) {
  // Just verify no crash when the SessionManager is destroyed without
  // connecting.
  EXPECT_NO_THROW({
    SessionManager session_manager;
    // SessionManager goes out of scope here
  });
}

// ============================================================================
// Track creation before connection
// ============================================================================

TEST_F(SessionManagerTest, CreateAudioTrackBeforeConnectThrows) {
  SessionManager session_manager;

  EXPECT_THROW(session_manager.createAudioTrack(
                   "mic", 48000, 2, livekit::TrackSource::SOURCE_MICROPHONE),
               std::runtime_error)
      << "createAudioTrack should throw when not connected";
}

TEST_F(SessionManagerTest, CreateVideoTrackBeforeConnectThrows) {
  SessionManager session_manager;

  EXPECT_THROW(session_manager.createVideoTrack(
                   "cam", 1280, 720, livekit::TrackSource::SOURCE_CAMERA),
               std::runtime_error)
      << "createVideoTrack should throw when not connected";
}

// ============================================================================
// Callback registration (pre-connection, pure map operations)
// ============================================================================

TEST_F(SessionManagerTest, RegisterAndUnregisterAudioCallbackDoesNotCrash) {
  SessionManager session_manager;

  EXPECT_NO_THROW({
    session_manager.setOnAudioFrameCallback(
        "remote-participant", livekit::TrackSource::SOURCE_MICROPHONE,
        [](const livekit::AudioFrame &) {});

    session_manager.clearOnAudioFrameCallback(
        "remote-participant", livekit::TrackSource::SOURCE_MICROPHONE);
  }) << "Registering and unregistering an audio callback should be safe "
        "even without a connection";
}

TEST_F(SessionManagerTest, RegisterAndUnregisterVideoCallbackDoesNotCrash) {
  SessionManager session_manager;

  EXPECT_NO_THROW({
    session_manager.setOnVideoFrameCallback(
        "remote-participant", livekit::TrackSource::SOURCE_CAMERA,
        [](const livekit::VideoFrame &, std::int64_t) {});

    session_manager.clearOnVideoFrameCallback(
        "remote-participant", livekit::TrackSource::SOURCE_CAMERA);
  }) << "Registering and unregistering a video callback should be safe "
        "even without a connection";
}

TEST_F(SessionManagerTest, UnregisterNonExistentCallbackIsNoOp) {
  SessionManager session_manager;

  EXPECT_NO_THROW({
    session_manager.clearOnAudioFrameCallback(
        "nonexistent", livekit::TrackSource::SOURCE_MICROPHONE);
    session_manager.clearOnVideoFrameCallback(
        "nonexistent", livekit::TrackSource::SOURCE_CAMERA);
  }) << "Unregistering a callback that was never registered should be a no-op";
}

TEST_F(SessionManagerTest, MultipleRegistrationsSameKeyOverwrites) {
  SessionManager session_manager;

  int call_count = 0;

  // Register a first callback
  session_manager.setOnAudioFrameCallback(
      "alice", livekit::TrackSource::SOURCE_MICROPHONE,
      [](const livekit::AudioFrame &) {});

  // Register a second callback for the same key -- should overwrite
  session_manager.setOnAudioFrameCallback(
      "alice", livekit::TrackSource::SOURCE_MICROPHONE,
      [&call_count](const livekit::AudioFrame &) { call_count++; });

  // Unregister once should be enough (only one entry per key)
  EXPECT_NO_THROW(session_manager.clearOnAudioFrameCallback(
      "alice", livekit::TrackSource::SOURCE_MICROPHONE));
}

TEST_F(SessionManagerTest, RegisterCallbacksForMultipleParticipants) {
  SessionManager session_manager;

  EXPECT_NO_THROW({
    session_manager.setOnAudioFrameCallback(
        "alice", livekit::TrackSource::SOURCE_MICROPHONE,
        [](const livekit::AudioFrame &) {});

    session_manager.setOnVideoFrameCallback(
        "bob", livekit::TrackSource::SOURCE_CAMERA,
        [](const livekit::VideoFrame &, std::int64_t) {});

    session_manager.setOnAudioFrameCallback(
        "charlie", livekit::TrackSource::SOURCE_SCREENSHARE_AUDIO,
        [](const livekit::AudioFrame &) {});
  }) << "Should be able to register callbacks for multiple participants";

  // Cleanup
  session_manager.clearOnAudioFrameCallback(
      "alice", livekit::TrackSource::SOURCE_MICROPHONE);
  session_manager.clearOnVideoFrameCallback(
      "bob", livekit::TrackSource::SOURCE_CAMERA);
  session_manager.clearOnAudioFrameCallback(
      "charlie", livekit::TrackSource::SOURCE_SCREENSHARE_AUDIO);
}

} // namespace test
} // namespace session_manager
