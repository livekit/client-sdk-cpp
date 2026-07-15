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

#include "room_proto_converter.h"
#include "video_utils.h"

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

TEST(VideoFrameMetadataTest, UserDataOnlyIsPreserved) {
  VideoFrameMetadata metadata;
  metadata.user_data = std::vector<std::uint8_t>{0x01, 0x02, 0xab, 0xcd};

  auto proto_metadata = toProto(metadata);
  ASSERT_TRUE(proto_metadata.has_value());
  EXPECT_FALSE(proto_metadata->has_user_timestamp());
  EXPECT_FALSE(proto_metadata->has_frame_id());
  EXPECT_TRUE(proto_metadata->has_user_data());
  EXPECT_EQ(proto_metadata->user_data(), std::string("\x01\x02\xab\xcd", 4));

  auto round_trip = fromProto(*proto_metadata);
  ASSERT_TRUE(round_trip.has_value());
  EXPECT_EQ(round_trip->user_timestamp_us, std::nullopt);
  EXPECT_EQ(round_trip->frame_id, std::nullopt);
  ASSERT_TRUE(round_trip->user_data.has_value());
  EXPECT_EQ(*round_trip->user_data, (std::vector<std::uint8_t>{0x01, 0x02, 0xab, 0xcd}));
}

TEST(VideoFrameMetadataTest, AllFieldsArePreserved) {
  VideoFrameMetadata metadata;
  metadata.user_timestamp_us = 123;
  metadata.frame_id = 456;
  metadata.user_data = std::vector<std::uint8_t>{0xde, 0xad};

  auto proto_metadata = toProto(metadata);
  ASSERT_TRUE(proto_metadata.has_value());
  EXPECT_TRUE(proto_metadata->has_user_timestamp());
  EXPECT_TRUE(proto_metadata->has_frame_id());
  EXPECT_TRUE(proto_metadata->has_user_data());

  auto round_trip = fromProto(*proto_metadata);
  ASSERT_TRUE(round_trip.has_value());
  EXPECT_EQ(round_trip->user_timestamp_us, std::optional<std::uint64_t>(123));
  EXPECT_EQ(round_trip->frame_id, std::optional<std::uint32_t>(456));
  ASSERT_TRUE(round_trip->user_data.has_value());
  EXPECT_EQ(*round_trip->user_data, (std::vector<std::uint8_t>{0xde, 0xad}));
}

TEST(VideoFrameMetadataTest, EmptyProtoMetadataIsIgnored) {
  proto::FrameMetadata metadata;
  EXPECT_FALSE(fromProto(metadata).has_value());
}

TEST(TrackPublishOptionsTest, FrameMetadataFeaturesRoundTrip) {
  TrackPublishOptions options;
  options.frame_metadata_features.emplace();
  options.frame_metadata_features->user_timestamp = true;
  options.frame_metadata_features->frame_id = true;
  options.frame_metadata_features->user_data = true;

  proto::TrackPublishOptions proto_options = toProto(options);
  ASSERT_EQ(proto_options.frame_metadata_features_size(), 3);
  EXPECT_EQ(proto_options.frame_metadata_features(0), proto::FrameMetadataFeature::FMF_USER_TIMESTAMP);
  EXPECT_EQ(proto_options.frame_metadata_features(1), proto::FrameMetadataFeature::FMF_FRAME_ID);
  EXPECT_EQ(proto_options.frame_metadata_features(2), proto::FrameMetadataFeature::FMF_USER_DATA);

  TrackPublishOptions round_trip = fromProto(proto_options);
  ASSERT_TRUE(round_trip.frame_metadata_features.has_value());
  EXPECT_TRUE(round_trip.frame_metadata_features->user_timestamp);
  EXPECT_TRUE(round_trip.frame_metadata_features->frame_id);
  EXPECT_TRUE(round_trip.frame_metadata_features->user_data);
}

TEST(TrackPublishOptionsTest, DegradationPreferenceRoundTrip) {
  TrackPublishOptions options;
  options.degradation_preference = DegradationPreference::MaintainFramerateAndResolution;

  proto::TrackPublishOptions proto_options = toProto(options);
  ASSERT_TRUE(proto_options.has_degradation_preference());
  EXPECT_EQ(proto_options.degradation_preference(),
            proto::DegradationPreference::DEGRADATION_PREFERENCE_MAINTAIN_FRAMERATE_AND_RESOLUTION);

  TrackPublishOptions round_trip = fromProto(proto_options);
  ASSERT_TRUE(round_trip.degradation_preference.has_value());
  EXPECT_EQ(*round_trip.degradation_preference, DegradationPreference::MaintainFramerateAndResolution);
}

TEST(TrackPublishOptionsTest, DeprecatedPacketTrailerFeaturesAreMerged) {
  TrackPublishOptions options;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
  PacketTrailerFeatures legacy_features;
  legacy_features.user_timestamp = true;
  legacy_features.frame_id = true;
  legacy_features.user_data = true;
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  options.packet_trailer_features = legacy_features;

  proto::TrackPublishOptions proto_options = toProto(options);
  ASSERT_EQ(proto_options.frame_metadata_features_size(), 3);
  EXPECT_EQ(proto_options.frame_metadata_features(0), proto::FrameMetadataFeature::FMF_USER_TIMESTAMP);
  EXPECT_EQ(proto_options.frame_metadata_features(1), proto::FrameMetadataFeature::FMF_FRAME_ID);
  EXPECT_EQ(proto_options.frame_metadata_features(2), proto::FrameMetadataFeature::FMF_USER_DATA);

  TrackPublishOptions round_trip = fromProto(proto_options);
  ASSERT_TRUE(round_trip.frame_metadata_features.has_value());
  EXPECT_TRUE(round_trip.frame_metadata_features->user_timestamp);
  EXPECT_TRUE(round_trip.frame_metadata_features->frame_id);
  EXPECT_TRUE(round_trip.frame_metadata_features->user_data);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
  EXPECT_TRUE(round_trip.packet_trailer_features.user_timestamp);
  EXPECT_TRUE(round_trip.packet_trailer_features.frame_id);
  EXPECT_TRUE(round_trip.packet_trailer_features.user_data);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}

} // namespace livekit::test
