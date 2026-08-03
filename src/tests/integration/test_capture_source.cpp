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

/// End-to-end test for capture sources (livekit-capture over FFI).
///
/// Publishes the built-in demo source — the capture pump pushes frames
/// server-side, so the test drives no frames itself — and verifies that a
/// second participant receives real video through the SFU.
///
/// Requires the Rust FFI built with the `capture` feature
/// (-DLIVEKIT_ENABLE_CAPTURE=ON); otherwise the test skips.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "../common/test_common.h"
#include "livekit/capture_source.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr int kMinFramesReceived = 5;
constexpr int kCaptureWidth = 1280;
constexpr int kCaptureHeight = 720;
constexpr std::uint32_t kCaptureFramerateFps = 30;

/// Creates the capture source, or skips the test when the FFI library was
/// built without the capture feature.
std::shared_ptr<CaptureSource> createDemoCaptureOrSkip() {
  DemoVideoSourceConfig config;
  config.resolution = {kCaptureWidth, kCaptureHeight};
  config.framerate_fps = kCaptureFramerateFps;
  try {
    return CaptureSource::create(config).get();
  } catch (const std::exception& e) {
    if (std::string(e.what()).find("without the 'capture' feature") != std::string::npos) {
      return nullptr;
    }
    throw;
  }
}

} // namespace

class CaptureSourceServerTest : public LiveKitTestBase {};

TEST_F(CaptureSourceServerTest, DemoCaptureSourcePublishesFramesEndToEnd) {
  failIfNotConfigured();

  auto capture = createDemoCaptureOrSkip();
  if (capture == nullptr) {
    GTEST_SKIP() << "livekit-ffi built without the 'capture' feature; "
                    "configure with -DLIVEKIT_ENABLE_CAPTURE=ON";
  }

  // The demo source reports back the resolution it was configured with.
  ASSERT_EQ(capture->kind(), CaptureSourceKind::Pixel);
  ASSERT_EQ(capture->width(), kCaptureWidth);
  ASSERT_EQ(capture->height(), kCaptureHeight);
  ASSERT_FALSE(capture->codec().has_value());
  ASSERT_NE(capture->videoSource(), nullptr);

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

  // Count frames arriving at the receiver: proof of media flowing through
  // the SFU without the test pushing a single frame.
  std::mutex mutex;
  std::condition_variable cv;
  int frames_received = 0;

  const std::string track_name = "demo-capture-track";
  receiver_room.setOnVideoFrameEventCallback(sender_identity, track_name,
                                             [&mutex, &cv, &frames_received](const VideoFrameEvent& /*event*/) {
                                               std::lock_guard<std::mutex> lock(mutex);
                                               if (++frames_received >= kMinFramesReceived) {
                                                 cv.notify_all();
                                               }
                                             });

  // Publish a track backed by the capture source's RTC video source,
  // merging application options over the source-derived ones.
  auto track = LocalVideoTrack::createLocalVideoTrack(track_name, capture->videoSource());

  // Application options are merged in; source-dictated fields win.
  TrackPublishOptions app_options;
  app_options.source = TrackSource::SOURCE_CAMERA;
  ASSERT_NO_THROW(lockLocalParticipant(sender_room)->publishTrack(track, capture->publishOptions(app_options)));

  // Observe the capture's terminal event.
  std::optional<CaptureResult> finished;
  capture->setOnFinishedCallback([&mutex, &cv, &finished](const CaptureResult& result) {
    std::lock_guard<std::mutex> lock(mutex);
    finished = result;
    cv.notify_all();
  });

  ASSERT_NO_THROW(capture->start());
  EXPECT_THROW(capture->start(), CaptureSourceError) << "double start must be rejected";

  {
    std::unique_lock<std::mutex> lock(mutex);
    const bool got_frames =
        cv.wait_for(lock, 30s, [&frames_received] { return frames_received >= kMinFramesReceived; });
    ASSERT_TRUE(got_frames) << "Timed out waiting for demo capture frames; received " << frames_received;
  }

  // Stop is a signal; the terminal callback delivers the stats.
  ASSERT_NO_THROW(capture->stop());
  {
    std::unique_lock<std::mutex> lock(mutex);
    const bool got_finished = cv.wait_for(lock, 10s, [&finished] { return finished.has_value(); });
    ASSERT_TRUE(got_finished) << "Timed out waiting for the capture finished callback";
  }

  ASSERT_FALSE(finished->error.has_value()) << *finished->error;
  EXPECT_EQ(finished->exit, CaptureExit::Stopped);
  EXPECT_GT(finished->frames_captured, 0u);

  receiver_room.clearOnVideoFrameCallback(sender_identity, track_name);
  if (track->publication()) {
    lockLocalParticipant(sender_room)->unpublishTrack(track->publication()->sid());
  }
}

} // namespace livekit::test
