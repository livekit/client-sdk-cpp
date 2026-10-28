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

/// Tests for capture sources (livekit-capture over FFI).
///
/// Publishes the built-in pattern and clock sources and verifies that a second
/// participant receives video through the SFU without per-frame FFI traffic.
///
/// All of them require the Rust FFI built with capture-source support
/// (-DLIVEKIT_CAPTURE=all); otherwise they skip.

#include <livekit/capture/capture_source.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "../common/test_common.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr int kMinFramesReceived = 5;
constexpr int kCaptureWidth = 1280;
constexpr int kCaptureHeight = 720;
constexpr std::uint32_t kCaptureFramerateFps = 30;

Result<std::shared_ptr<capture::CaptureSource>, capture::CaptureError> createCapture(bool clock) {
  if (clock) {
    capture::ClockVideoSourceConfig config;
    config.resolution = {kCaptureWidth, kCaptureHeight};
    config.framerate_fps = kCaptureFramerateFps;
    return capture::CaptureSource::create(config).get();
  }
  capture::PatternVideoSourceConfig config;
  config.resolution = {kCaptureWidth, kCaptureHeight};
  config.framerate_fps = kCaptureFramerateFps;
  return capture::CaptureSource::create(config).get();
}

} // namespace

class CaptureSourceServerTest : public LiveKitTestBase, public ::testing::WithParamInterface<bool> {};

TEST_P(CaptureSourceServerTest, CaptureSourcePublishesFramesEndToEnd) {
#if !LIVEKIT_CAPTURE_ENABLED
  GTEST_SKIP() << "livekit-ffi built without capture support; configure with -DLIVEKIT_CAPTURE=all";
#endif
  failIfNotConfigured();

  const auto creation_started = std::chrono::steady_clock::now();
  auto created = createCapture(GetParam());
  ASSERT_TRUE(created.ok()) << created.error().message;
  auto capture = std::move(created).value();
  RecordProperty(
      "capture_creation_ms",
      std::to_string(
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - creation_started).count()));
  ASSERT_NE(capture, nullptr);

  // The pixel source reports back the resolution it was configured with.
  ASSERT_EQ(capture->kind(), capture::CaptureSourceKind::Pixel);
  ASSERT_EQ(capture->width(), kCaptureWidth);
  ASSERT_EQ(capture->height(), kCaptureHeight);
  ASSERT_FALSE(capture->codec().has_value());
  ASSERT_NE(capture->videoSource(), nullptr);

  Room sender_room;
  Room receiver_room;
  const RoomOptions options;

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

  const std::string track_name = "pixel-capture-track";
  receiver_room.setOnVideoFrameEventCallback(sender_identity, track_name,
                                             [&mutex, &cv, &frames_received](const VideoFrameEvent& /*event*/) {
                                               const std::scoped_lock lock(mutex);
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

  const auto capture_started = std::chrono::steady_clock::now();
  const auto started = capture->start();
  ASSERT_TRUE(started.ok()) << started.error().message;
  EXPECT_THROW(static_cast<void>(capture->start()), std::runtime_error) << "double start must be rejected";

  {
    std::unique_lock<std::mutex> lock(mutex);
    const bool got_frames =
        cv.wait_for(lock, 30s, [&frames_received] { return frames_received >= kMinFramesReceived; });
    ASSERT_TRUE(got_frames) << "Timed out waiting for capture frames; received " << frames_received;
  }

  // Stop is a signal; join synchronizes with shutdown and returns the stats.
  const auto stopped = capture->stop();
  ASSERT_TRUE(stopped.ok()) << stopped.error().message;
  auto joined = capture->join();
  ASSERT_EQ(joined.wait_for(10s), std::future_status::ready) << "Timed out waiting for capture join";
  const auto finished = joined.get();

  ASSERT_TRUE(finished.ok()) << finished.error().message;
  EXPECT_EQ(finished.value().exit, capture::CaptureExit::Stopped);
  EXPECT_GT(finished.value().frames_captured, 0u);
  const auto capture_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - capture_started).count();
  RecordProperty("capture_frames_per_second",
                 std::to_string(static_cast<double>(finished.value().frames_captured) / capture_seconds));

  receiver_room.clearOnVideoFrameCallback(sender_identity, track_name);
  if (track->publication()) {
    lockLocalParticipant(sender_room)->unpublishTrack(track->publication()->sid());
  }
}

INSTANTIATE_TEST_SUITE_P(PixelSources, CaptureSourceServerTest, ::testing::Bool(),
                         [](const ::testing::TestParamInfo<bool>& info) { return info.param ? "Clock" : "Pattern"; });

} // namespace livekit::test
