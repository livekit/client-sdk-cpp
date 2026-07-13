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
#include <livekit/data_track_info.h>

namespace livekit::test {

TEST(DataTrackInfoTest, DefaultConstructed) {
  DataTrackInfo info;
  EXPECT_TRUE(info.name.empty());
  EXPECT_TRUE(info.sid.empty());
  EXPECT_FALSE(info.uses_e2ee);
  EXPECT_FALSE(info.schema.has_value());
  EXPECT_FALSE(info.frame_encoding.has_value());
}

TEST(DataTrackInfoTest, AggregateInitialization) {
  DataTrackInfo info{"name", "sid", true, DataTrackSchemaId{"schema", DataTrackSchemaEncoding::JsonSchema},
                     DataTrackFrameEncoding::Json};
  EXPECT_EQ(info.name, "name");
  EXPECT_EQ(info.sid, "sid");
  EXPECT_TRUE(info.uses_e2ee);
  ASSERT_TRUE(info.schema.has_value());
  EXPECT_EQ(*info.schema, (DataTrackSchemaId{"schema", DataTrackSchemaEncoding::JsonSchema}));
  ASSERT_TRUE(info.frame_encoding.has_value());
  EXPECT_EQ(*info.frame_encoding, DataTrackFrameEncoding::Json);
}

} // namespace livekit::test
