/*
 * Copyright 2025 LiveKit
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
#include "tests/common/test_common.h"
#include "video_utils.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace livekit {
namespace test {

TEST(VideoFrameMetadataTest, EmptyMetadataIsOmittedFromProto) {
  std::optional<VideoFrameMetadata> metadata = VideoFrameMetadata{};
  EXPECT_FALSE(toProto(metadata).has_value());
}

TEST(VideoFrameMetadataTest, UserTimestampOnlyIsPreserved) {
  VideoFrameMetadata metadata;
  metadata.user_timestamp_us = 123;

  auto proto_metadata = toProto(metadata);
  ASSERT_TRUE(proto_metadata.has_value());
  EXPECT_TRUE(proto_metadata->has_user_timestamp_us());
  EXPECT_EQ(proto_metadata->user_timestamp_us(), 123u);
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
  EXPECT_FALSE(proto_metadata->has_user_timestamp_us());
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
  EXPECT_TRUE(proto_metadata->has_user_timestamp_us());
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

class VideoFrameMetadataServerTest : public LiveKitTestBase {};

TEST_F(VideoFrameMetadataServerTest,
       UserTimestampRoundTripsToReceiverEventCallback) {
  skipIfNotConfigured();

  Room sender_room;
  Room receiver_room;
  RoomOptions options;

  ASSERT_TRUE(receiver_room.Connect(config_.url, config_.receiver_token, options));
  ASSERT_TRUE(sender_room.Connect(config_.url, config_.caller_token, options));
  ASSERT_NE(sender_room.localParticipant(), nullptr);
  ASSERT_NE(receiver_room.localParticipant(), nullptr);

  const std::string sender_identity = sender_room.localParticipant()->identity();
  ASSERT_FALSE(sender_identity.empty());
  ASSERT_TRUE(waitForParticipant(&receiver_room, sender_identity, 10s));

  std::mutex mutex;
  std::condition_variable cv;
  std::optional<std::uint64_t> received_user_timestamp_us;

  receiver_room.setOnVideoFrameEventCallback(
      sender_identity, TrackSource::SOURCE_CAMERA,
      [&mutex, &cv, &received_user_timestamp_us](const VideoFrameEvent &event) {
        std::lock_guard<std::mutex> lock(mutex);
        if (event.metadata && event.metadata->user_timestamp_us.has_value()) {
          received_user_timestamp_us = event.metadata->user_timestamp_us;
          cv.notify_all();
        }
      });

  auto source = std::make_shared<VideoSource>(16, 16);
  auto track = LocalVideoTrack::createLocalVideoTrack("metadata-track", source);

  TrackPublishOptions publish_options;
  publish_options.source = TrackSource::SOURCE_CAMERA;
  publish_options.simulcast = false;
  publish_options.packet_trailer_features.user_timestamp = true;

  ASSERT_NO_THROW(sender_room.localParticipant()->publishTrack(track, publish_options));

  VideoFrame frame = VideoFrame::create(16, 16, VideoBufferType::RGBA);
  std::fill(frame.data(), frame.data() + frame.dataSize(), 0x7f);

  const std::uint64_t expected_user_timestamp_us = getTimestampUs();
  VideoCaptureOptions capture_options;
  capture_options.timestamp_us =
      static_cast<std::int64_t>(expected_user_timestamp_us);
  capture_options.metadata = VideoFrameMetadata{};
  capture_options.metadata->user_timestamp_us = expected_user_timestamp_us;

  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (std::chrono::steady_clock::now() < deadline) {
    source->captureFrame(frame, capture_options);

    std::unique_lock<std::mutex> lock(mutex);
    if (cv.wait_for(lock, 100ms, [&received_user_timestamp_us] {
          return received_user_timestamp_us.has_value();
        })) {
      break;
    }
  }

  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(received_user_timestamp_us.has_value())
        << "Timed out waiting for user timestamp metadata";
    EXPECT_EQ(*received_user_timestamp_us, expected_user_timestamp_us);
  }

  receiver_room.clearOnVideoFrameCallback(sender_identity,
                                          TrackSource::SOURCE_CAMERA);
  if (track->publication()) {
    sender_room.localParticipant()->unpublishTrack(track->publication()->sid());
  }
}

} // namespace test
} // namespace livekit
