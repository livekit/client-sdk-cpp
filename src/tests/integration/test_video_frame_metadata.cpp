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

#include "tests/common/test_common.h"
#include "video_utils.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace livekit::test {

class VideoFrameMetadataServerTest : public LiveKitTestBase {};

TEST_F(VideoFrameMetadataServerTest,
       UserTimestampRoundTripsToReceiverEventCallback) {
  skipIfNotConfigured();

  Room sender_room;
  Room receiver_room;
  RoomOptions options;

  ASSERT_TRUE(
      receiver_room.Connect(config_.url, config_.receiver_token, options));
  ASSERT_TRUE(sender_room.Connect(config_.url, config_.caller_token, options));
  ASSERT_NE(sender_room.localParticipant(), nullptr);
  ASSERT_NE(receiver_room.localParticipant(), nullptr);

  const std::string sender_identity =
      sender_room.localParticipant()->identity();
  ASSERT_FALSE(sender_identity.empty());
  ASSERT_TRUE(waitForParticipant(&receiver_room, sender_identity, 10s));

  std::mutex mutex;
  std::condition_variable cv;
  std::optional<std::uint64_t> received_user_timestamp_us;

  const std::string track_name = "metadata-track";
  receiver_room.setOnVideoFrameEventCallback(
      sender_identity, track_name,
      [&mutex, &cv, &received_user_timestamp_us](const VideoFrameEvent &event) {
        std::lock_guard<std::mutex> lock(mutex);
        if (event.metadata && event.metadata->user_timestamp.has_value()) {
          received_user_timestamp_us = event.metadata->user_timestamp;
          cv.notify_all();
        }
      });

  auto source = std::make_shared<VideoSource>(16, 16);
  auto track = LocalVideoTrack::createLocalVideoTrack(track_name, source);

  TrackPublishOptions publish_options;
  publish_options.source = TrackSource::SOURCE_CAMERA;
  publish_options.simulcast = false;
  publish_options.packet_trailer_features.user_timestamp = true;

  ASSERT_NO_THROW(
      sender_room.localParticipant()->publishTrack(track, publish_options));

  const auto track_ready_deadline = std::chrono::steady_clock::now() + 10s;
  bool receiver_track_ready = false;
  while (std::chrono::steady_clock::now() < track_ready_deadline) {
    auto *sender_on_receiver = receiver_room.remoteParticipant(sender_identity);
    if (sender_on_receiver != nullptr) {
      for (const auto &[sid, publication] :
           sender_on_receiver->trackPublications()) {
        (void)sid;
        if (publication == nullptr) {
          continue;
        }

        if (publication->name() != track_name ||
            publication->kind() != TrackKind::KIND_VIDEO) {
          continue;
        }

        if (publication->subscribed() && publication->track() != nullptr) {
          receiver_track_ready = true;
          break;
        }
      }
    }

    if (receiver_track_ready) {
      break;
    }

    std::this_thread::sleep_for(10ms);
  }

  ASSERT_TRUE(receiver_track_ready)
      << "Timed out waiting for receiver video track subscription";

  VideoFrame frame = VideoFrame::create(16, 16, VideoBufferType::RGBA);
  std::fill(frame.data(), frame.data() + frame.dataSize(), 0x7f);

  const std::uint64_t expected_user_timestamp_us = getTimestampUs();
  VideoCaptureOptions capture_options;
  capture_options.timestamp_us =
      static_cast<std::int64_t>(expected_user_timestamp_us);
  capture_options.metadata = VideoFrameMetadata{};
  capture_options.metadata->user_timestamp = expected_user_timestamp_us;

  source->captureFrame(frame, capture_options);

  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(cv.wait_for(lock, 10s, [&received_user_timestamp_us] {
      return received_user_timestamp_us.has_value();
    }))
        << "Timed out waiting for user timestamp metadata";
    EXPECT_EQ(*received_user_timestamp_us, expected_user_timestamp_us);
  }

  receiver_room.clearOnVideoFrameCallback(sender_identity, track_name);
  if (track->publication()) {
    sender_room.localParticipant()->unpublishTrack(track->publication()->sid());
  }
}

} // namespace livekit::test
