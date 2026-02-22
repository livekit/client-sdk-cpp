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
#include <livekit_bridge/bridge_audio_track.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace livekit_bridge {
namespace test {

class BridgeAudioTrackTest : public ::testing::Test {
protected:
  /// Create a BridgeAudioTrack with null SDK objects for pure-logic testing.
  /// The track is usable for accessor and state management tests but will
  /// crash if pushFrame / mute / unmute try to dereference SDK pointers
  /// on a non-released track.
  static BridgeAudioTrack createNullTrack(const std::string &name = "mic",
                                          int sample_rate = 48000,
                                          int num_channels = 2) {
    return BridgeAudioTrack(name, sample_rate, num_channels,
                            nullptr, // source
                            nullptr, // track
                            nullptr, // publication
                            nullptr  // participant
    );
  }
};

TEST_F(BridgeAudioTrackTest, AccessorsReturnConstructionValues) {
  auto track = createNullTrack("test-mic", 16000, 1);

  EXPECT_EQ(track.name(), "test-mic") << "Name should match construction value";
  EXPECT_EQ(track.sampleRate(), 16000) << "Sample rate should match";
  EXPECT_EQ(track.numChannels(), 1) << "Channel count should match";
}

TEST_F(BridgeAudioTrackTest, InitiallyNotReleased) {
  auto track = createNullTrack();

  EXPECT_FALSE(track.isReleased())
      << "Track should not be released immediately after construction";
}

TEST_F(BridgeAudioTrackTest, ReleaseMarksTrackAsReleased) {
  auto track = createNullTrack();

  track.release();

  EXPECT_TRUE(track.isReleased())
      << "Track should be released after calling release()";
}

TEST_F(BridgeAudioTrackTest, DoubleReleaseIsIdempotent) {
  auto track = createNullTrack();

  track.release();
  EXPECT_NO_THROW(track.release())
      << "Calling release() a second time should be a no-op";
  EXPECT_TRUE(track.isReleased());
}

TEST_F(BridgeAudioTrackTest, PushFrameAfterReleaseReturnsFalse) {
  auto track = createNullTrack();
  track.release();

  std::vector<std::int16_t> data(960, 0);

  EXPECT_FALSE(track.pushFrame(data, 480))
      << "pushFrame (vector) on a released track should return false";
}

TEST_F(BridgeAudioTrackTest, PushFrameRawPointerAfterReleaseReturnsFalse) {
  auto track = createNullTrack();
  track.release();

  std::vector<std::int16_t> data(960, 0);

  EXPECT_FALSE(track.pushFrame(data.data(), 480))
      << "pushFrame (raw pointer) on a released track should return false";
}

TEST_F(BridgeAudioTrackTest, MuteOnReleasedTrackDoesNotCrash) {
  auto track = createNullTrack();
  track.release();

  EXPECT_NO_THROW(track.mute())
      << "mute() on a released track should be a no-op";
}

TEST_F(BridgeAudioTrackTest, UnmuteOnReleasedTrackDoesNotCrash) {
  auto track = createNullTrack();
  track.release();

  EXPECT_NO_THROW(track.unmute())
      << "unmute() on a released track should be a no-op";
}

} // namespace test
} // namespace livekit_bridge
