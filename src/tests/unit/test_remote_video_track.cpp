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

#include <livekit/remote_video_track.h>

#include "track.pb.h"

namespace livekit::test {

TEST(RemoteVideoTrackTest, ConstructFromEmptyOwnedTrack) {
  proto::OwnedTrack owned;
  RemoteVideoTrack track(owned);

  EXPECT_TRUE(track.sid().empty());
  EXPECT_TRUE(track.name().empty());
  EXPECT_TRUE(track.remote());
  EXPECT_FALSE(track.has_handle());

  const std::string description = track.to_string();
  EXPECT_FALSE(description.empty());
}

} // namespace livekit::test
