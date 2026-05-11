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

#include <livekit/data_track_error.h>

#include "data_track.pb.h"

namespace livekit::test {

TEST(DataTrackErrorTest, PublishErrorFromEmptyProto) {
  proto::PublishDataTrackError proto_err;
  PublishDataTrackError err = PublishDataTrackError::fromProto(proto_err);
  EXPECT_EQ(err.code, PublishDataTrackErrorCode::UNKNOWN);
}

TEST(DataTrackErrorTest, TryPushErrorFromEmptyProto) {
  proto::LocalDataTrackTryPushError proto_err;
  LocalDataTrackTryPushError err = LocalDataTrackTryPushError::fromProto(proto_err);
  EXPECT_EQ(err.code, LocalDataTrackTryPushErrorCode::UNKNOWN);
}

TEST(DataTrackErrorTest, SubscribeErrorFromEmptyProto) {
  proto::SubscribeDataTrackError proto_err;
  SubscribeDataTrackError err = SubscribeDataTrackError::fromProto(proto_err);
  EXPECT_EQ(err.code, SubscribeDataTrackErrorCode::UNKNOWN);
}

} // namespace livekit::test
