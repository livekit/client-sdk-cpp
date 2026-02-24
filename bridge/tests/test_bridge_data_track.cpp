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

#include <gtest/gtest.h>
#include <livekit_bridge/bridge_data_track.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace livekit_bridge {
namespace test {

class BridgeDataTrackTest : public ::testing::Test {
protected:
  /// Create a BridgeDataTrack with a null SDK track for pure-logic testing.
  /// The track is usable for accessor and state management tests but
  /// pushFrame / isPublished will return false without a real LocalDataTrack.
  static BridgeDataTrack createNullTrack(const std::string &name = "data") {
    return BridgeDataTrack(name, std::shared_ptr<livekit::LocalDataTrack>{});
  }
};

TEST_F(BridgeDataTrackTest, AccessorsReturnConstructionValues) {
  auto track = createNullTrack("sensor-data");

  EXPECT_EQ(track.name(), "sensor-data")
      << "Name should match construction value";
}

TEST_F(BridgeDataTrackTest, InitiallyNotReleased) {
  auto track = createNullTrack();

  EXPECT_FALSE(track.isReleased())
      << "Track should not be released immediately after construction";
}

TEST_F(BridgeDataTrackTest, ReleaseMarksTrackAsReleased) {
  auto track = createNullTrack();

  track.release();

  EXPECT_TRUE(track.isReleased())
      << "Track should be released after calling release()";
}

TEST_F(BridgeDataTrackTest, DoubleReleaseIsIdempotent) {
  auto track = createNullTrack();

  track.release();
  EXPECT_NO_THROW(track.release())
      << "Calling release() a second time should be a no-op";
  EXPECT_TRUE(track.isReleased());
}

TEST_F(BridgeDataTrackTest, PushFrameVectorAfterReleaseReturnsFalse) {
  auto track = createNullTrack();
  track.release();

  std::vector<std::uint8_t> payload = {0x01, 0x02, 0x03};

  EXPECT_FALSE(track.pushFrame(payload))
      << "pushFrame (vector) on a released track should return false";
}

TEST_F(BridgeDataTrackTest, PushFrameVectorWithTimestampAfterReleaseReturnsFalse) {
  auto track = createNullTrack();
  track.release();

  std::vector<std::uint8_t> payload = {0x01, 0x02};
  EXPECT_FALSE(track.pushFrame(payload, 12345u))
      << "pushFrame (vector, timestamp) on a released track should return false";
}

TEST_F(BridgeDataTrackTest, PushFrameRawPointerAfterReleaseReturnsFalse) {
  auto track = createNullTrack();
  track.release();

  std::vector<std::uint8_t> payload = {0x01, 0x02, 0x03};

  EXPECT_FALSE(track.pushFrame(payload.data(), payload.size()))
      << "pushFrame (raw pointer) on a released track should return false";
}

TEST_F(BridgeDataTrackTest, PushFrameRawPointerWithTimestampAfterReleaseReturnsFalse) {
  auto track = createNullTrack();
  track.release();

  std::uint8_t data[] = {0xab, 0xcd};
  EXPECT_FALSE(track.pushFrame(data, 2, 99999u))
      << "pushFrame (raw pointer, timestamp) on a released track should return false";
}

TEST_F(BridgeDataTrackTest, PushFrameWithNullTrackReturnsFalse) {
  auto track = createNullTrack();

  std::vector<std::uint8_t> payload = {0x01};
  EXPECT_FALSE(track.pushFrame(payload))
      << "pushFrame with null underlying track should return false";
}

TEST_F(BridgeDataTrackTest, IsPublishedWithNullTrackReturnsFalse) {
  auto track = createNullTrack();

  EXPECT_FALSE(track.isPublished())
      << "isPublished() with null track should return false";
}

TEST_F(BridgeDataTrackTest, IsPublishedAfterReleaseReturnsFalse) {
  auto track = createNullTrack();
  track.release();

  EXPECT_FALSE(track.isPublished())
      << "isPublished() after release() should return false";
}

} // namespace test
} // namespace livekit_bridge
