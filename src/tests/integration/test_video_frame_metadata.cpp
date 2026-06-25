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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "tests/common/test_common.h"

namespace livekit::test {

class VideoFrameMetadataServerTest : public LiveKitTestBase {};

TEST_F(VideoFrameMetadataServerTest, UserTimestampRoundTripsToReceiverEventCallback) {
  failIfNotConfigured();

  Room sender_room;
  Room receiver_room;
  RoomOptions options;

  ASSERT_TRUE(receiver_room.connect(config_.url, config_.token_b, options));
  ASSERT_TRUE(sender_room.connect(config_.url, config_.token_a, options));
  ASSERT_FALSE(sender_room.localParticipant().expired());
  ASSERT_FALSE(receiver_room.localParticipant().expired());

  const std::string sender_identity = lockLocalParticipant(sender_room)->identity();
  ASSERT_FALSE(sender_identity.empty());
  ASSERT_TRUE(waitForParticipant(&receiver_room, sender_identity, 10s));

  std::mutex mutex;
  std::condition_variable cv;
  std::optional<std::uint64_t> received_user_timestamp_us;

  const std::string track_name = "metadata-track";
  receiver_room.setOnVideoFrameEventCallback(sender_identity, track_name,
                                             [&mutex, &cv, &received_user_timestamp_us](const VideoFrameEvent& event) {
                                               std::lock_guard<std::mutex> lock(mutex);
                                               if (!event.metadata) {
                                                 return;
                                               }
                                               const auto& user_timestamp_us = event.metadata->user_timestamp_us;
                                               if (user_timestamp_us.has_value() && *user_timestamp_us != 0) {
                                                 received_user_timestamp_us = user_timestamp_us;
                                                 cv.notify_all();
                                               }
                                             });

  auto source = std::make_shared<VideoSource>(16, 16);
  auto track = LocalVideoTrack::createLocalVideoTrack(track_name, source);

  TrackPublishOptions publish_options;
  publish_options.source = TrackSource::SOURCE_CAMERA;
  publish_options.simulcast = false;
  publish_options.packet_trailer_features.user_timestamp = true;

  ASSERT_NO_THROW(lockLocalParticipant(sender_room)->publishTrack(track, publish_options));

  const auto track_ready_deadline = std::chrono::steady_clock::now() + 10s;
  bool receiver_track_ready = false;
  while (std::chrono::steady_clock::now() < track_ready_deadline) {
    auto sender_on_receiver = receiver_room.remoteParticipant(sender_identity).lock();
    if (sender_on_receiver != nullptr) {
      for (const auto& [sid, publication] : sender_on_receiver->trackPublications()) {
        (void)sid;
        if (publication == nullptr) {
          continue;
        }

        if (publication->name() != track_name || publication->kind() != TrackKind::KIND_VIDEO) {
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

  ASSERT_TRUE(receiver_track_ready) << "Timed out waiting for receiver video track subscription";

  std::atomic<bool> publishing{true};
  std::thread publisher([&]() {
    VideoFrame frame = VideoFrame::create(16, 16, VideoBufferType::RGBA);
    std::fill(frame.data(), frame.data() + frame.dataSize(), 0x7f);

    while (publishing.load(std::memory_order_relaxed)) {
      const std::uint64_t user_timestamp_us = getTimestampUs();
      VideoCaptureOptions capture_options;
      capture_options.timestamp_us = static_cast<std::int64_t>(user_timestamp_us);
      capture_options.metadata = VideoFrameMetadata{};
      capture_options.metadata->user_timestamp_us = user_timestamp_us;

      try {
        source->captureFrame(frame, capture_options);
      } catch (...) {
        publishing.store(false, std::memory_order_relaxed);
        break;
      }
      std::this_thread::sleep_for(50ms);
    }
  });

  bool got_metadata = false;
  {
    std::unique_lock<std::mutex> lock(mutex);
    got_metadata =
        cv.wait_for(lock, 10s, [&received_user_timestamp_us] { return received_user_timestamp_us.has_value(); });
  }

  publishing.store(false, std::memory_order_relaxed);
  publisher.join();

  std::optional<std::uint64_t> received_user_timestamp_snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex);
    received_user_timestamp_snapshot = received_user_timestamp_us;
  }

  receiver_room.clearOnVideoFrameCallback(sender_identity, track_name);
  if (track->publication()) {
    lockLocalParticipant(sender_room)->unpublishTrack(track->publication()->sid());
  }

  ASSERT_TRUE(got_metadata) << "Timed out waiting for user timestamp metadata";
  ASSERT_TRUE(received_user_timestamp_snapshot.has_value());

  const auto received_at = getTimestampUs();
  ASSERT_LE(*received_user_timestamp_snapshot, received_at);
  EXPECT_LT(received_at - *received_user_timestamp_snapshot, 1000000u);
}

TEST_F(VideoFrameMetadataServerTest, UserDataRoundTripsToReceiverEventCallback) {
  failIfNotConfigured();

  Room sender_room;
  Room receiver_room;
  RoomOptions options;

  ASSERT_TRUE(receiver_room.connect(config_.url, config_.token_b, options));
  ASSERT_TRUE(sender_room.connect(config_.url, config_.token_a, options));
  ASSERT_FALSE(sender_room.localParticipant().expired());
  ASSERT_FALSE(receiver_room.localParticipant().expired());

  const std::string sender_identity = lockLocalParticipant(sender_room)->identity();
  ASSERT_FALSE(sender_identity.empty());
  ASSERT_TRUE(waitForParticipant(&receiver_room, sender_identity, 10s));

  std::mutex mutex;
  std::condition_variable cv;
  std::optional<std::vector<std::uint8_t>> received_user_data;

  const std::string track_name = "userdata-track";
  const std::vector<std::uint8_t> expected_user_data{0x01, 0x02, 0xab, 0xcd, 0xef};

  receiver_room.setOnVideoFrameEventCallback(sender_identity, track_name,
                                             [&mutex, &cv, &received_user_data](const VideoFrameEvent& event) {
                                               std::lock_guard<std::mutex> lock(mutex);
                                               if (!event.metadata || !event.metadata->user_data.has_value()) {
                                                 return;
                                               }
                                               if (!event.metadata->user_data->empty()) {
                                                 received_user_data = event.metadata->user_data;
                                                 cv.notify_all();
                                               }
                                             });

  auto source = std::make_shared<VideoSource>(16, 16);
  auto track = LocalVideoTrack::createLocalVideoTrack(track_name, source);

  TrackPublishOptions publish_options;
  publish_options.source = TrackSource::SOURCE_CAMERA;
  publish_options.simulcast = false;
  publish_options.packet_trailer_features.user_data = true;

  ASSERT_NO_THROW(lockLocalParticipant(sender_room)->publishTrack(track, publish_options));

  const auto track_ready_deadline = std::chrono::steady_clock::now() + 10s;
  bool receiver_track_ready = false;
  while (std::chrono::steady_clock::now() < track_ready_deadline) {
    auto sender_on_receiver = receiver_room.remoteParticipant(sender_identity).lock();
    if (sender_on_receiver != nullptr) {
      for (const auto& [sid, publication] : sender_on_receiver->trackPublications()) {
        (void)sid;
        if (publication == nullptr) {
          continue;
        }

        if (publication->name() != track_name || publication->kind() != TrackKind::KIND_VIDEO) {
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

  ASSERT_TRUE(receiver_track_ready) << "Timed out waiting for receiver video track subscription";

  std::atomic<bool> publishing{true};
  std::thread publisher([&]() {
    VideoFrame frame = VideoFrame::create(16, 16, VideoBufferType::RGBA);
    std::fill(frame.data(), frame.data() + frame.dataSize(), 0x7f);

    while (publishing.load(std::memory_order_relaxed)) {
      VideoCaptureOptions capture_options;
      capture_options.timestamp_us = static_cast<std::int64_t>(getTimestampUs());
      capture_options.metadata = VideoFrameMetadata{};
      capture_options.metadata->user_data = expected_user_data;

      try {
        source->captureFrame(frame, capture_options);
      } catch (...) {
        publishing.store(false, std::memory_order_relaxed);
        break;
      }
      std::this_thread::sleep_for(50ms);
    }
  });

  bool got_metadata = false;
  {
    std::unique_lock<std::mutex> lock(mutex);
    got_metadata = cv.wait_for(lock, 10s, [&received_user_data] { return received_user_data.has_value(); });
  }

  publishing.store(false, std::memory_order_relaxed);
  publisher.join();

  std::optional<std::vector<std::uint8_t>> received_user_data_snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex);
    received_user_data_snapshot = received_user_data;
  }

  receiver_room.clearOnVideoFrameCallback(sender_identity, track_name);
  if (track->publication()) {
    lockLocalParticipant(sender_room)->unpublishTrack(track->publication()->sid());
  }

  ASSERT_TRUE(got_metadata) << "Timed out waiting for user data metadata";
  ASSERT_TRUE(received_user_data_snapshot.has_value());
  EXPECT_EQ(*received_user_data_snapshot, expected_user_data);
}

} // namespace livekit::test
