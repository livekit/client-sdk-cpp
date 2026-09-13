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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <livekit/encoded_video_source.h>
#include <livekit/livekit.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace livekit::test {

class EncodedVideoSourceTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogLevel::Info); }
  void TearDown() override { livekit::shutdown(); }
};

TEST_F(EncodedVideoSourceTest, ConstructAndQueryProperties) {
  const EncodedVideoSource source(VideoCodec::H264, 640, 480);
  EXPECT_EQ(source.width(), 640);
  EXPECT_EQ(source.height(), 480);
  EXPECT_EQ(source.codec(), VideoCodec::H264);
  EXPECT_NE(source.ffiHandleId(), 0U);
}

TEST_F(EncodedVideoSourceTest, RejectsInvalidDimensions) {
  EXPECT_THROW((void)EncodedVideoSource(VideoCodec::H264, 0, 480), std::invalid_argument);
  EXPECT_THROW((void)EncodedVideoSource(VideoCodec::H264, 640, -1), std::invalid_argument);
  EXPECT_THROW((void)EncodedVideoSource(VideoCodec::H264, 65536, 480), std::invalid_argument);
}

TEST_F(EncodedVideoSourceTest, RejectsInvalidFramesBeforeFfi) {
  const EncodedVideoSource source(VideoCodec::H264, 640, 480);
  EncodedVideoSource::Frame frame;

  EXPECT_THROW((void)source.captureFrame(frame), std::invalid_argument);

  frame.data = {0x01};
  frame.width = 640;
  EXPECT_THROW((void)source.captureFrame(frame), std::invalid_argument);

  // Dimensions above the signed range the encoder path uses must be rejected
  // here rather than wrapping negative downstream.
  frame.width = 65536;
  frame.height = 480;
  EXPECT_THROW((void)source.captureFrame(frame), std::invalid_argument);

  frame.width = 640;
  frame.height = std::numeric_limits<std::uint32_t>::max();
  EXPECT_THROW((void)source.captureFrame(frame), std::invalid_argument);
}

TEST_F(EncodedVideoSourceTest, InitialFeedbackIsEmpty) {
  const EncodedVideoSource source(VideoCodec::H264, 640, 480);
  const EncodedVideoSource::Feedback feedback = source.takeFeedback();
  EXPECT_FALSE(feedback.keyframe_requested);
  EXPECT_FALSE(feedback.rate_control.has_value());
}

TEST_F(EncodedVideoSourceTest, SubmitsOwnedAccessUnit) {
  const EncodedVideoSource source(VideoCodec::H264, 16, 16);
  EncodedVideoSource::Frame frame;
  frame.is_keyframe = true;
  frame.timestamp_us = 1234;
  frame.data = {0x00, 0x00, 0x00, 0x01, 0x65, 0x88};

  EXPECT_TRUE(source.captureFrame(frame));
}

} // namespace livekit::test
