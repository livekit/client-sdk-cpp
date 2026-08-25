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
/// Two groups. `CaptureSourceServerTest` publishes the built-in pattern source —
/// the capture pump pushes frames server-side, so the test drives no frames
/// itself — and verifies that a second participant receives real video through
/// the SFU. `CaptureDeviceTest` covers camera device enumeration and format
/// negotiation, which need no room; those tests skip when the machine has no
/// camera.
///
/// All of them require the Rust FFI built with the `capture` feature
/// (-DLIVEKIT_ENABLE_CAPTURE=ON); otherwise they skip.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../common/test_common.h"
#include "livekit/capture_source.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr int kMinFramesReceived = 5;
constexpr int kCaptureWidth = 1280;
constexpr int kCaptureHeight = 720;
constexpr std::uint32_t kCaptureFramerateFps = 30;

/// Skips the calling test when the FFI library was built without the capture
/// feature. A feature-less FFI reports only a generic invalid handle, so this
/// has to be decided at compile time rather than sniffed from an error string.
#ifdef LIVEKIT_TEST_CAPTURE_ENABLED
#define SKIP_WITHOUT_CAPTURE_FEATURE() ((void)0)
#else
#define SKIP_WITHOUT_CAPTURE_FEATURE() \
  GTEST_SKIP() << "livekit-ffi built without the 'capture' feature; configure with -DLIVEKIT_ENABLE_CAPTURE=ON"
#endif

std::shared_ptr<CaptureSource> createPatternCapture() {
  PatternVideoSourceConfig config;
  config.resolution = {kCaptureWidth, kCaptureHeight};
  config.framerate_fps = kCaptureFramerateFps;
  return CaptureSource::create(config).get();
}

} // namespace

class CaptureSourceServerTest : public LiveKitTestBase {};

/// Device tests need an initialized SDK but no room, so they share the base
/// fixture without requiring the server environment variables.
class CaptureDeviceTest : public LiveKitTestBase {};

/// Device enumeration is a pure local query: it needs no server, and a
/// machine with no camera is a valid (empty) result.
TEST_F(CaptureDeviceTest, ListDevicesReportsWellFormedDevices) {
  SKIP_WITHOUT_CAPTURE_FEATURE();

  std::vector<CaptureDeviceInfo> devices;
  try {
    devices = CaptureSource::listDevices().get();
  } catch (const CaptureSourceError& e) {
    // A platform with no capture backend reports UnsupportedPlatform.
    GTEST_SKIP() << "capture device enumeration unavailable: " << e.what();
  }

  for (const CaptureDeviceInfo& device : devices) {
    EXPECT_FALSE(device.id.empty()) << "device id must be a stable identifier";
    EXPECT_FALSE(device.name.empty()) << "device name must be human-readable";
    for (const DeviceFormat& format : device.formats) {
      EXPECT_GT(format.resolution.width, 0);
      EXPECT_GT(format.resolution.height, 0);
      EXPECT_GT(format.framerate_fps, 0u);
    }
    // A device reporting a complete format list must report at least one.
    if (device.formats_complete) {
      EXPECT_FALSE(device.formats.empty()) << "device " << device.id << " claims a complete but empty format list";
    }
  }
}

/// An unsatisfiable exact format request must fail loudly at construction
/// rather than silently falling back to another format.
///
/// The resolution is deliberately odd-sized rather than merely large: no
/// camera offers 7x3, and its odd dimensions survive any driver rounding to a
/// macroblock or 2-pixel boundary.
///
/// CaptureSourceError carries only a message, so this cannot assert on a
/// specific cause. The preceding skips narrow it: the capture feature is
/// present and at least one device enumerated, so the throw is attributable to
/// the format request.
TEST_F(CaptureDeviceTest, ExactFormatRequestRejectsUnsupportedFormat) {
  SKIP_WITHOUT_CAPTURE_FEATURE();

  std::vector<CaptureDeviceInfo> devices;
  try {
    devices = CaptureSource::listDevices().get();
  } catch (const CaptureSourceError& e) {
    GTEST_SKIP() << "capture device enumeration unavailable: " << e.what();
  }
  if (devices.empty()) {
    GTEST_SKIP() << "no capture devices attached to this machine";
  }

  DeviceVideoSourceConfig config;
  config.device = DeviceSelector::id(devices.front().id);
  // NV12 is requestable on every backend, so the rejection is attributable to
  // the resolution rather than to frame-format validation.
  config.format = DeviceFormatRequest::exact(DeviceFormat{{7, 3}, 1, DeviceFrameFormat::NV12});

  EXPECT_THROW(CaptureSource::create(config).get(), CaptureSourceError);
}

/// Opening the platform default device with the default format request must
/// negotiate a usable format and report the negotiated resolution.
TEST_F(CaptureDeviceTest, DefaultDeviceNegotiatesAFormat) {
  SKIP_WITHOUT_CAPTURE_FEATURE();

  std::vector<CaptureDeviceInfo> devices;
  try {
    devices = CaptureSource::listDevices().get();
  } catch (const CaptureSourceError& e) {
    GTEST_SKIP() << "capture device enumeration unavailable: " << e.what();
  }
  if (devices.empty()) {
    GTEST_SKIP() << "no capture devices attached to this machine";
  }

  std::shared_ptr<CaptureSource> capture;
  try {
    capture = CaptureSource::create(DeviceVideoSourceConfig{}).get();
  } catch (const CaptureSourceError& e) {
    // A camera present but claimed by another process, or denied by the
    // platform's privacy controls, is not a binding failure.
    GTEST_SKIP() << "default capture device unavailable: " << e.what();
  }

  ASSERT_NE(capture, nullptr);
  EXPECT_EQ(capture->kind(), CaptureSourceKind::Pixel);
  EXPECT_GT(capture->width(), 0);
  EXPECT_GT(capture->height(), 0);
  EXPECT_FALSE(capture->codec().has_value()) << "device capture is a pixel source";
  EXPECT_NE(capture->videoSource(), nullptr);
}

TEST_F(CaptureSourceServerTest, PatternCaptureSourcePublishesFramesEndToEnd) {
  SKIP_WITHOUT_CAPTURE_FEATURE();
  failIfNotConfigured();

  auto capture = createPatternCapture();
  ASSERT_NE(capture, nullptr);

  // The pattern source reports back the resolution it was configured with.
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

  const std::string track_name = "pattern-capture-track";
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
    ASSERT_TRUE(got_frames) << "Timed out waiting for pattern capture frames; received " << frames_received;
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
