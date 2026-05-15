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
#include <livekit/audio_stream.h>
#include <livekit/data_track_stream.h>
#include <livekit/video_frame.h>
#include <livekit/video_stream.h>

#include <cstddef>

namespace livekit::test {

TEST(StreamOptionsTest, AudioStreamOptionsDefaults) {
  AudioStream::Options options;
  EXPECT_EQ(options.capacity, 0u);
  EXPECT_TRUE(options.noise_cancellation_module.empty());
  EXPECT_TRUE(options.noise_cancellation_options_json.empty());
}

TEST(StreamOptionsTest, VideoStreamOptionsDefaults) {
  VideoStream::Options options;
  EXPECT_EQ(options.capacity, 0u);
  EXPECT_EQ(options.format, VideoBufferType::RGBA);
}

TEST(StreamOptionsTest, DataTrackStreamOptionsDefaults) {
  DataTrackStream::Options options;
  EXPECT_FALSE(options.buffer_size.has_value());
}

TEST(StreamOptionsTest, AudioFrameEventIsConstructible) {
  AudioFrameEvent ev;
  (void)ev;
  SUCCEED();
}

TEST(StreamOptionsTest, VideoFrameEventIsConstructible) {
  VideoFrameEvent ev;
  ev.timestamp_us = 0;
  ev.rotation = VideoRotation::VIDEO_ROTATION_0;
  EXPECT_FALSE(ev.metadata.has_value());
}

} // namespace livekit::test
