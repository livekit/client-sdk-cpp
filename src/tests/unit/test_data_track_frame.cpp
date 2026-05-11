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

#include <cstdint>
#include <vector>

#include <livekit/data_track_frame.h>

#include "data_track.pb.h"

namespace livekit::test {

TEST(DataTrackFrameTest, DefaultConstructed) {
  DataTrackFrame frame;
  EXPECT_TRUE(frame.payload.empty());
  EXPECT_FALSE(frame.user_timestamp.has_value());
}

TEST(DataTrackFrameTest, PayloadAndTimestampConstructor) {
  std::vector<std::uint8_t> payload{1, 2, 3};
  DataTrackFrame frame(std::move(payload), 12345u);
  ASSERT_EQ(frame.payload.size(), 3u);
  EXPECT_EQ(frame.payload[0], 1u);
  ASSERT_TRUE(frame.user_timestamp.has_value());
  EXPECT_EQ(*frame.user_timestamp, 12345u);
}

TEST(DataTrackFrameTest, FromOwnedInfoEmptyProto) {
  proto::DataTrackFrame proto_frame;
  DataTrackFrame frame = DataTrackFrame::fromOwnedInfo(proto_frame);
  EXPECT_TRUE(frame.payload.empty());
  EXPECT_FALSE(frame.user_timestamp.has_value());
}

} // namespace livekit::test
