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
#include <livekit/video_frame.h>

#include <cstdint>
#include <vector>

namespace livekit::test {

TEST(VideoFrameTest, DefaultConstructed) {
  VideoFrame frame;
  EXPECT_EQ(frame.dataSize(), 0u);
}

TEST(VideoFrameTest, ConstructFromBuffer) {
  std::vector<std::uint8_t> data(std::size_t{16} * 16 * 4, 0xAB);
  VideoFrame frame(16, 16, VideoBufferType::RGBA, std::move(data));
  EXPECT_EQ(frame.width(), 16);
  EXPECT_EQ(frame.height(), 16);
  EXPECT_EQ(frame.type(), VideoBufferType::RGBA);
  EXPECT_EQ(frame.dataSize(), 16u * 16u * 4u);
}

TEST(VideoFrameTest, FactoryCreate) {
  VideoFrame frame = VideoFrame::create(8, 8, VideoBufferType::ARGB);
  EXPECT_EQ(frame.width(), 8);
  EXPECT_EQ(frame.height(), 8);
  EXPECT_EQ(frame.type(), VideoBufferType::ARGB);
  EXPECT_GT(frame.dataSize(), 0u);

  const auto planes = frame.planeInfos();
  EXPECT_FALSE(planes.empty());
}

} // namespace livekit::test
