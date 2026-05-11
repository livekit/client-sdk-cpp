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
}

TEST(DataTrackInfoTest, AggregateInitialization) {
  DataTrackInfo info{"name", "sid", true};
  EXPECT_EQ(info.name, "name");
  EXPECT_EQ(info.sid, "sid");
  EXPECT_TRUE(info.uses_e2ee);
}

} // namespace livekit::test
