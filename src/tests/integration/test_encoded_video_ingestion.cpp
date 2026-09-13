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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "tests/common/test_common.h"

namespace livekit::test {
namespace {

// One 16x16 baseline-profile H.264 Annex-B key access unit. It contains AUD,
// SPS, PPS, and IDR NAL units and was generated from a black I420 frame.
constexpr std::array<std::uint8_t, 59> kH264KeyAccessUnit = {
    0x00, 0x00, 0x00, 0x01, 0x09, 0x10, 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0a, 0xdd,
    0xe8, 0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x05, 0x23, 0xc4, 0x89, 0xe0, 0x00,
    0x00, 0x00, 0x01, 0x68, 0xce, 0x0f, 0xc8, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x3a, 0x11,
    0x8a, 0x00, 0x02, 0x4a, 0xb1, 0xc0, 0x00, 0x44, 0x66, 0x38, 0x00, 0x08, 0x8c, 0xe0};

} // namespace

class EncodedVideoIngestionIntegrationTest : public LiveKitTestBase {};

TEST_F(EncodedVideoIngestionIntegrationTest, PublisherSendsPreEncodedVideoToReceiver) {
  failIfNotConfigured();

  Room sender_room;
  Room receiver_room;
  const RoomOptions room_options;
  ASSERT_TRUE(receiver_room.connect(config_.url, config_.token_b, room_options));
  ASSERT_TRUE(sender_room.connect(config_.url, config_.token_a, room_options));

  const std::string sender_identity = lockLocalParticipant(sender_room)->identity();
  ASSERT_TRUE(waitForParticipant(&receiver_room, sender_identity, 10s));

  std::mutex mutex;
  std::condition_variable cv;
  bool received_frame = false;
  constexpr char kTrackName[] = "pre-encoded-h264";
  receiver_room.setOnVideoFrameEventCallback(sender_identity, kTrackName,
                                             [&mutex, &cv, &received_frame](const VideoFrameEvent& event) {
                                               if (event.frame.width() != 16 || event.frame.height() != 16) {
                                                 return;
                                               }
                                               {
                                                 const std::scoped_lock lock(mutex);
                                                 received_frame = true;
                                               }
                                               cv.notify_all();
                                             });

  auto source = std::make_shared<EncodedVideoSource>(VideoCodec::H264, 16, 16);
  auto track = LocalVideoTrack::createLocalVideoTrack(kTrackName, source);
  TrackPublishOptions publish_options;
  publish_options.video_codec = VideoCodec::H264;
  publish_options.video_encoder = VideoEncoderBackend::PreEncoded;
  publish_options.simulcast = false;
  publish_options.source = TrackSource::SOURCE_CAMERA;
  ASSERT_NO_THROW(lockLocalParticipant(sender_room)->publishTrack(track, publish_options));

  std::atomic<bool> publishing{true};
  std::thread publisher([&source, &publishing]() {
    EncodedVideoSource::Frame frame;
    frame.is_keyframe = true;
    frame.data.assign(kH264KeyAccessUnit.begin(), kH264KeyAccessUnit.end());
    while (publishing.load(std::memory_order_relaxed)) {
      frame.timestamp_us = static_cast<std::int64_t>(getTimestampUs());
      try {
        (void)source->captureFrame(frame);
      } catch (...) {
        publishing.store(false, std::memory_order_relaxed);
        break;
      }
      std::this_thread::sleep_for(100ms);
    }
  });

  bool received = false;
  {
    std::unique_lock<std::mutex> lock(mutex);
    received = cv.wait_for(lock, 10s, [&received_frame] { return received_frame; });
  }

  publishing.store(false, std::memory_order_relaxed);
  publisher.join();
  receiver_room.clearOnVideoFrameCallback(sender_identity, kTrackName);
  if (track->publication()) {
    lockLocalParticipant(sender_room)->unpublishTrack(track->publication()->sid());
  }

  EXPECT_TRUE(received) << "Timed out waiting for a decoded pre-encoded H.264 frame";
}

} // namespace livekit::test
