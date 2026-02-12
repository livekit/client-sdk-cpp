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
#include <livekit_bridge/bridge_video_track.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace livekit_bridge {
namespace test {

class BridgeVideoTrackTest : public ::testing::Test {
protected:
  /// Create a BridgeVideoTrack with null SDK objects for pure-logic testing.
  static BridgeVideoTrack createNullTrack(const std::string &name = "cam",
                                          int width = 1280, int height = 720) {
    return BridgeVideoTrack(name, width, height,
                            nullptr, // source
                            nullptr, // track
                            nullptr, // publication
                            nullptr  // participant
    );
  }
};

TEST_F(BridgeVideoTrackTest, AccessorsReturnConstructionValues) {
  auto track = createNullTrack("test-cam", 640, 480);

  EXPECT_EQ(track.name(), "test-cam") << "Name should match construction value";
  EXPECT_EQ(track.width(), 640) << "Width should match";
  EXPECT_EQ(track.height(), 480) << "Height should match";
}

TEST_F(BridgeVideoTrackTest, InitiallyNotReleased) {
  auto track = createNullTrack();

  EXPECT_FALSE(track.isReleased())
      << "Track should not be released immediately after construction";
}

TEST_F(BridgeVideoTrackTest, ReleaseMarksTrackAsReleased) {
  auto track = createNullTrack();

  track.release();

  EXPECT_TRUE(track.isReleased())
      << "Track should be released after calling release()";
}

TEST_F(BridgeVideoTrackTest, DoubleReleaseIsIdempotent) {
  auto track = createNullTrack();

  track.release();
  EXPECT_NO_THROW(track.release())
      << "Calling release() a second time should be a no-op";
  EXPECT_TRUE(track.isReleased());
}

TEST_F(BridgeVideoTrackTest, PushFrameAfterReleaseThrows) {
  auto track = createNullTrack();
  track.release();

  std::vector<std::uint8_t> data(1280 * 720 * 4, 0);

  EXPECT_THROW(track.pushFrame(data), std::runtime_error)
      << "pushFrame (vector) on a released track should throw";
}

TEST_F(BridgeVideoTrackTest, PushFrameRawPointerAfterReleaseThrows) {
  auto track = createNullTrack();
  track.release();

  std::vector<std::uint8_t> data(1280 * 720 * 4, 0);

  EXPECT_THROW(track.pushFrame(data.data(), data.size()), std::runtime_error)
      << "pushFrame (raw pointer) on a released track should throw";
}

TEST_F(BridgeVideoTrackTest, MuteOnReleasedTrackDoesNotCrash) {
  auto track = createNullTrack();
  track.release();

  EXPECT_NO_THROW(track.mute())
      << "mute() on a released track should be a no-op";
}

TEST_F(BridgeVideoTrackTest, UnmuteOnReleasedTrackDoesNotCrash) {
  auto track = createNullTrack();
  track.release();

  EXPECT_NO_THROW(track.unmute())
      << "unmute() on a released track should be a no-op";
}

} // namespace test
} // namespace livekit_bridge
