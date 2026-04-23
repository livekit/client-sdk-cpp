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

#include "room_proto_converter.h"
#include "video_utils.h"

#include <gtest/gtest.h>

namespace livekit::test {

TEST(VideoFrameMetadataTest, EmptyMetadataIsOmittedFromProto) {
  std::optional<VideoFrameMetadata> metadata = VideoFrameMetadata{};
  EXPECT_FALSE(toProto(metadata).has_value());
}

TEST(VideoFrameMetadataTest, UserTimestampOnlyIsPreserved) {
  VideoFrameMetadata metadata;
  metadata.user_timestamp_us = 123;

  auto proto_metadata = toProto(metadata);
  ASSERT_TRUE(proto_metadata.has_value());
  EXPECT_TRUE(proto_metadata->has_user_timestamp());
  EXPECT_EQ(proto_metadata->user_timestamp(), 123u);
  EXPECT_FALSE(proto_metadata->has_frame_id());

  auto round_trip = fromProto(*proto_metadata);
  ASSERT_TRUE(round_trip.has_value());
  EXPECT_EQ(round_trip->user_timestamp_us, std::optional<std::uint64_t>(123));
  EXPECT_EQ(round_trip->frame_id, std::nullopt);
}

TEST(VideoFrameMetadataTest, FrameIdOnlyIsPreserved) {
  VideoFrameMetadata metadata;
  metadata.frame_id = 456;

  auto proto_metadata = toProto(metadata);
  ASSERT_TRUE(proto_metadata.has_value());
  EXPECT_FALSE(proto_metadata->has_user_timestamp());
  EXPECT_TRUE(proto_metadata->has_frame_id());
  EXPECT_EQ(proto_metadata->frame_id(), 456u);

  auto round_trip = fromProto(*proto_metadata);
  ASSERT_TRUE(round_trip.has_value());
  EXPECT_EQ(round_trip->user_timestamp_us, std::nullopt);
  EXPECT_EQ(round_trip->frame_id, std::optional<std::uint32_t>(456));
}

TEST(VideoFrameMetadataTest, BothFieldsArePreserved) {
  VideoFrameMetadata metadata;
  metadata.user_timestamp_us = 123;
  metadata.frame_id = 456;

  auto proto_metadata = toProto(metadata);
  ASSERT_TRUE(proto_metadata.has_value());
  EXPECT_TRUE(proto_metadata->has_user_timestamp());
  EXPECT_TRUE(proto_metadata->has_frame_id());

  auto round_trip = fromProto(*proto_metadata);
  ASSERT_TRUE(round_trip.has_value());
  EXPECT_EQ(round_trip->user_timestamp_us, std::optional<std::uint64_t>(123));
  EXPECT_EQ(round_trip->frame_id, std::optional<std::uint32_t>(456));
}

TEST(VideoFrameMetadataTest, EmptyProtoMetadataIsIgnored) {
  proto::FrameMetadata metadata;
  EXPECT_FALSE(fromProto(metadata).has_value());
}

TEST(TrackPublishOptionsTest, PacketTrailerFeaturesRoundTrip) {
  TrackPublishOptions options;
  options.packet_trailer_features.user_timestamp = true;
  options.packet_trailer_features.frame_id = true;

  proto::TrackPublishOptions proto_options = toProto(options);
  ASSERT_EQ(proto_options.packet_trailer_features_size(), 2);
  EXPECT_EQ(proto_options.packet_trailer_features(0),
            proto::PacketTrailerFeature::PTF_USER_TIMESTAMP);
  EXPECT_EQ(proto_options.packet_trailer_features(1),
            proto::PacketTrailerFeature::PTF_FRAME_ID);

  TrackPublishOptions round_trip = fromProto(proto_options);
  EXPECT_TRUE(round_trip.packet_trailer_features.user_timestamp);
  EXPECT_TRUE(round_trip.packet_trailer_features.frame_id);
}

} // namespace livekit::test
