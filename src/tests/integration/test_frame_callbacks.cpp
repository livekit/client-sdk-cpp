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
#include <thread>

#include "tests/common/test_common.h"

namespace livekit::test {

using namespace std::chrono_literals;

class FrameCallbackServerTest : public LiveKitTestBase {};

TEST_F(FrameCallbackServerTest, VideoCallbackRegisteredAfterSubscriptionReceivesFrames) {
  failIfNotConfigured();

  Room receiver_room;
  Room sender_room;
  RoomOptions options;
  options.auto_subscribe = true;

  ASSERT_TRUE(receiver_room.connect(config_.url, config_.token_b, options));
  ASSERT_TRUE(sender_room.connect(config_.url, config_.token_a, options));

  const std::string sender_identity = lockLocalParticipant(sender_room)->identity();
  ASSERT_TRUE(waitForParticipant(&receiver_room, sender_identity, 10s));

  const std::string track_name = "late-video-callback";
  auto source = std::make_shared<VideoSource>(16, 16);
  auto track = LocalVideoTrack::createLocalVideoTrack(track_name, source);

  TrackPublishOptions publish_options;
  publish_options.source = TrackSource::SOURCE_CAMERA;
  publish_options.simulcast = false;
  ASSERT_NO_THROW(lockLocalParticipant(sender_room)->publishTrack(track, publish_options));

  const auto subscription_deadline = std::chrono::steady_clock::now() + 10s;
  bool subscribed = false;
  while (std::chrono::steady_clock::now() < subscription_deadline && !subscribed) {
    auto sender_on_receiver = receiver_room.remoteParticipant(sender_identity).lock();
    if (sender_on_receiver != nullptr) {
      for (const auto& [sid, publication] : sender_on_receiver->trackPublications()) {
        (void)sid;
        if (publication != nullptr && publication->name() == track_name && publication->subscribed() &&
            publication->track() != nullptr) {
          subscribed = true;
          break;
        }
      }
    }
    if (!subscribed) {
      std::this_thread::sleep_for(10ms);
    }
  }
  ASSERT_TRUE(subscribed) << "Timed out waiting for the remote video subscription";

  std::mutex frame_mutex;
  std::condition_variable frame_cv;
  int received_frames = 0;
  std::thread registrar([&]() {
    receiver_room.setOnVideoFrameCallback(sender_identity, track_name, [&](const VideoFrame&, std::int64_t) {
      {
        const std::scoped_lock<std::mutex> lock(frame_mutex);
        ++received_frames;
      }
      frame_cv.notify_all();
    });
  });
  registrar.join();

  std::atomic<bool> publishing{true};
  std::thread publisher([&]() {
    VideoFrame frame = VideoFrame::create(16, 16, VideoBufferType::RGBA);
    std::fill(frame.data(), frame.data() + frame.dataSize(), 0x7f);
    while (publishing.load(std::memory_order_relaxed)) {
      try {
        source->captureFrame(frame);
      } catch (...) {
        publishing.store(false, std::memory_order_relaxed);
        break;
      }
      std::this_thread::sleep_for(50ms);
    }
  });

  bool received = false;
  {
    std::unique_lock<std::mutex> lock(frame_mutex);
    received = frame_cv.wait_for(lock, 10s, [&]() { return received_frames > 0; });
  }

  publishing.store(false, std::memory_order_relaxed);
  publisher.join();
  receiver_room.clearOnVideoFrameCallback(sender_identity, track_name);
  if (track->publication()) {
    lockLocalParticipant(sender_room)->unpublishTrack(track->publication()->sid());
  }

  EXPECT_TRUE(received) << "No video frames arrived after late callback registration";
}

} // namespace livekit::test
