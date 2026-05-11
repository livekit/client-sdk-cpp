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

#include <livekit/livekit.h>
#include <livekit/video_source.h>

namespace livekit::test {

class VideoSourceTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole); }
  void TearDown() override { livekit::shutdown(); }
};

TEST_F(VideoSourceTest, ConstructAndQueryProperties) {
  VideoSource source(640, 480);
  EXPECT_EQ(source.width(), 640);
  EXPECT_EQ(source.height(), 480);
  EXPECT_NE(source.ffi_handle_id(), 0u);
}

TEST_F(VideoSourceTest, VideoCaptureOptionsDefaults) {
  VideoCaptureOptions options;
  EXPECT_EQ(options.timestamp_us, 0);
  EXPECT_EQ(options.rotation, VideoRotation::VIDEO_ROTATION_0);
  EXPECT_FALSE(options.metadata.has_value());
}

} // namespace livekit::test
