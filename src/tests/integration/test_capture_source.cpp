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

/// End-to-end tests for the built-in pattern and clock capture sources.
/// The Rust pump produces frames without C++ per-frame FFI calls.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
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
enum class SourceVariant { Gradient, Logo, Clock };

struct CaptureTestState {
  std::mutex mutex;
  std::condition_variable cv;
  int frames_received = 0;
  std::optional<CaptureResult> finished;
};

/// Skips the calling test when the FFI library was built without the capture
/// feature. A feature-less FFI reports only a generic invalid handle, so this
/// has to be decided at compile time rather than sniffed from an error string.
#define SKIP_WITHOUT_CAPTURE_FEATURE()                                                                               \
  do {                                                                                                               \
    if constexpr (!kCaptureEnabled) {                                                                                \
      GTEST_SKIP() << "livekit-ffi built without the 'capture' feature; configure with -DLIVEKIT_ENABLE_CAPTURE=ON"; \
    }                                                                                                                \
  } while (false)

std::shared_ptr<CaptureSource> createCapture(SourceVariant variant) {
  if (variant == SourceVariant::Clock) {
    return CaptureSource::create(ClockVideoSourceConfig{{kCaptureWidth, kCaptureHeight}, kCaptureFramerateFps}).get();
  }
  const Pattern pattern = variant == SourceVariant::Logo ? Pattern::Logo : Pattern::Gradient;
  return CaptureSource::create(PatternVideoSourceConfig{{kCaptureWidth, kCaptureHeight}, kCaptureFramerateFps, pattern})
      .get();
}

} // namespace

class CaptureSourceServerTest : public LiveKitTestBase, public ::testing::WithParamInterface<SourceVariant> {};

TEST_P(CaptureSourceServerTest, PublishesFramesEndToEnd) {
  SKIP_WITHOUT_CAPTURE_FEATURE();
  failIfNotConfigured();

  std::shared_ptr<CaptureSource> capture;
  try {
    capture = createCapture(GetParam());
  } catch (const CaptureSourceError& error) {
    if (std::string(error.what()).find("no compatible GPU adapter") != std::string::npos) {
      GTEST_SKIP() << "GPU capture unavailable: " << error.what();
    }
    throw;
  }
  ASSERT_NE(capture, nullptr);

  // Each source reports back the resolution it was configured with.
  ASSERT_EQ(capture->kind(), CaptureSourceKind::Pixel);
  ASSERT_EQ(capture->width(), kCaptureWidth);
  ASSERT_EQ(capture->height(), kCaptureHeight);
  ASSERT_FALSE(capture->codec().has_value());
  ASSERT_NE(capture->videoSource(), nullptr);

  TrackPublishOptions requested;
  requested.video_codec = VideoCodec::H264;
  requested.simulcast = false;
  const TrackPublishOptions merged = capture->publishOptions(requested);
  EXPECT_EQ(merged.video_codec, requested.video_codec);
  EXPECT_EQ(merged.simulcast, requested.simulcast);

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
  auto state = std::make_shared<CaptureTestState>();

  const std::string track_name = GetParam() == SourceVariant::Clock ? "clock-capture-track" : "pattern-capture-track";
  receiver_room.setOnVideoFrameEventCallback(sender_identity, track_name, [state](const VideoFrameEvent& /*event*/) {
    const std::scoped_lock lock(state->mutex);
    if (++state->frames_received >= kMinFramesReceived) {
      state->cv.notify_all();
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
  capture->setOnFinishedCallback([state](const CaptureResult& result) {
    const std::scoped_lock lock(state->mutex);
    state->finished = result;
    state->cv.notify_all();
  });

  ASSERT_TRUE(capture->start());
  EXPECT_FALSE(capture->start()) << "double start must be rejected";

  {
    std::unique_lock<std::mutex> lock(state->mutex);
    const bool got_frames =
        state->cv.wait_for(lock, 30s, [state] { return state->frames_received >= kMinFramesReceived; });
    ASSERT_TRUE(got_frames) << "Timed out waiting for capture frames; received " << state->frames_received;
  }

  // Stop is a signal; the terminal callback delivers the stats.
  ASSERT_TRUE(capture->stop());
  CaptureResult result;
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    const bool got_finished = state->cv.wait_for(lock, 10s, [state] { return state->finished.has_value(); });
    ASSERT_TRUE(got_finished) << "Timed out waiting for the capture finished callback";
    result = state->finished.value_or(CaptureResult{});
  }

  ASSERT_FALSE(result.error.has_value()) << result.error.value_or("");
  EXPECT_EQ(result.exit, CaptureExit::Stopped);
  EXPECT_GT(result.frames_captured, 0u);

  receiver_room.clearOnVideoFrameCallback(sender_identity, track_name);
  if (track->publication()) {
    lockLocalParticipant(sender_room)->unpublishTrack(track->publication()->sid());
  }
}

INSTANTIATE_TEST_SUITE_P(BuiltInSources, CaptureSourceServerTest,
                         ::testing::Values(SourceVariant::Gradient, SourceVariant::Logo, SourceVariant::Clock));

} // namespace livekit::test
