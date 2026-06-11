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
#include <livekit/livekit.h>
#include <livekit/video_source.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ffi.pb.h"
#include "ffi_client.h"

namespace livekit::test {

namespace {

class TestEncodedObserver : public EncodedVideoSourceObserver {
public:
  void onKeyframeRequested() override { ++keyframe_requests; }

  void onTargetBitrate(std::uint32_t bitrate_bps, double framerate_fps) override {
    ++target_bitrate_changes;
    last_bitrate_bps = bitrate_bps;
    last_framerate_fps = framerate_fps;
  }

  int keyframe_requests = 0;
  int target_bitrate_changes = 0;
  std::uint32_t last_bitrate_bps = 0;
  double last_framerate_fps = 0.0;
};

void pushEncodedSourceEvent(const proto::FfiEvent& event) {
  std::string bytes;
  ASSERT_TRUE(event.SerializeToString(&bytes));
  ffiEventCallback(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

proto::FfiEvent makeKeyframeRequestedEvent(std::uint64_t source_handle) {
  proto::FfiEvent event;
  auto* source_event = event.mutable_encoded_video_source_event();
  source_event->set_source_handle(source_handle);
  (void)source_event->mutable_keyframe_requested();
  return event;
}

proto::FfiEvent makeTargetBitrateChangedEvent(std::uint64_t source_handle, std::uint32_t bitrate_bps,
                                              double framerate_fps) {
  proto::FfiEvent event;
  auto* source_event = event.mutable_encoded_video_source_event();
  source_event->set_source_handle(source_handle);
  auto* target_bitrate = source_event->mutable_target_bitrate_changed();
  target_bitrate->set_bitrate_bps(bitrate_bps);
  target_bitrate->set_framerate_fps(framerate_fps);
  return event;
}

} // namespace

class VideoSourceTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogLevel::Info); }
  void TearDown() override { livekit::shutdown(); }
};

TEST_F(VideoSourceTest, ConstructAndQueryProperties) {
  VideoSource source(640, 480);
  EXPECT_EQ(source.width(), 640);
  EXPECT_EQ(source.height(), 480);
  EXPECT_NE(source.ffiHandleId(), 0u);
}

TEST_F(VideoSourceTest, VideoCaptureOptionsDefaults) {
  VideoCaptureOptions options;
  EXPECT_EQ(options.timestamp_us, 0);
  EXPECT_EQ(options.rotation, VideoRotation::VIDEO_ROTATION_0);
  EXPECT_FALSE(options.metadata.has_value());
}

TEST_F(VideoSourceTest, EncodedVideoSourceOptionsDefaultToH264) {
  EncodedVideoSourceOptions options;
  EXPECT_EQ(options.codec, VideoCodec::H264);
}

TEST_F(VideoSourceTest, EncodedVideoFrameInfoDefaults) {
  EncodedVideoFrameInfo info;
  EXPECT_FALSE(info.is_keyframe);
  EXPECT_FALSE(info.has_sps_pps);
  EXPECT_EQ(info.width, 0u);
  EXPECT_EQ(info.height, 0u);
  EXPECT_EQ(info.capture_time_us, 0);
}

TEST_F(VideoSourceTest, ConstructEncodedSourceAndQueryProperties) {
  EncodedVideoSourceOptions options;
  options.codec = VideoCodec::H264;

  VideoSource source(640, 480, options);
  EXPECT_EQ(source.width(), 640);
  EXPECT_EQ(source.height(), 480);
  EXPECT_NE(source.ffiHandleId(), 0u);
}

TEST_F(VideoSourceTest, NativeSourceRejectsEncodedCapture) {
  VideoSource source(640, 480);
  const std::vector<std::uint8_t> data{0x00, 0x00, 0x01, 0x65};
  EncodedVideoFrameInfo info;

  EXPECT_THROW((void)source.captureEncodedFrame(data, info), std::runtime_error);
}

TEST_F(VideoSourceTest, NativeSourceRejectsEncodedObserver) {
  VideoSource source(640, 480);
  auto observer = std::make_shared<TestEncodedObserver>();

  EXPECT_THROW(source.setEncodedObserver(observer), std::runtime_error);
}

TEST_F(VideoSourceTest, EncodedCaptureRejectsNullDataWithNonZeroSize) {
  VideoSource source(640, 480, EncodedVideoSourceOptions{});
  EncodedVideoFrameInfo info;

  EXPECT_THROW((void)source.captureEncodedFrame(nullptr, 1, info), std::invalid_argument);
}

TEST_F(VideoSourceTest, EncodedCaptureAllowsEmptyPayload) {
  VideoSource source(640, 480, EncodedVideoSourceOptions{});
  EncodedVideoFrameInfo info;
  bool accepted = false;

  EXPECT_NO_THROW(accepted = source.captureEncodedFrame(nullptr, 0, info));
  (void)accepted;
}

TEST_F(VideoSourceTest, EncodedObserverReceivesKeyframeRequestForMatchingSource) {
  VideoSource source(640, 480, EncodedVideoSourceOptions{});
  auto observer = std::make_shared<TestEncodedObserver>();
  source.setEncodedObserver(observer);

  pushEncodedSourceEvent(makeKeyframeRequestedEvent(source.ffiHandleId()));

  EXPECT_EQ(observer->keyframe_requests, 1);
  EXPECT_EQ(observer->target_bitrate_changes, 0);
}

TEST_F(VideoSourceTest, EncodedObserverReceivesTargetBitrateForMatchingSource) {
  VideoSource source(640, 480, EncodedVideoSourceOptions{});
  auto observer = std::make_shared<TestEncodedObserver>();
  source.setEncodedObserver(observer);

  pushEncodedSourceEvent(makeTargetBitrateChangedEvent(source.ffiHandleId(), 750000u, 29.97));

  EXPECT_EQ(observer->keyframe_requests, 0);
  ASSERT_EQ(observer->target_bitrate_changes, 1);
  EXPECT_EQ(observer->last_bitrate_bps, 750000u);
  EXPECT_DOUBLE_EQ(observer->last_framerate_fps, 29.97);
}

TEST_F(VideoSourceTest, EncodedObserverIgnoresOtherSourceHandle) {
  VideoSource source(640, 480, EncodedVideoSourceOptions{});
  auto observer = std::make_shared<TestEncodedObserver>();
  source.setEncodedObserver(observer);

  pushEncodedSourceEvent(makeKeyframeRequestedEvent(source.ffiHandleId() + 1u));
  pushEncodedSourceEvent(makeTargetBitrateChangedEvent(source.ffiHandleId() + 1u, 750000u, 30.0));

  EXPECT_EQ(observer->keyframe_requests, 0);
  EXPECT_EQ(observer->target_bitrate_changes, 0);
}

TEST_F(VideoSourceTest, EncodedObserverCanBeCleared) {
  VideoSource source(640, 480, EncodedVideoSourceOptions{});
  auto observer = std::make_shared<TestEncodedObserver>();
  source.setEncodedObserver(observer);
  source.setEncodedObserver(nullptr);

  pushEncodedSourceEvent(makeKeyframeRequestedEvent(source.ffiHandleId()));

  EXPECT_EQ(observer->keyframe_requests, 0);
  EXPECT_EQ(observer->target_bitrate_changes, 0);
}

TEST_F(VideoSourceTest, EncodedObserverSurvivesMoveConstruction) {
  VideoSource source(640, 480, EncodedVideoSourceOptions{});
  const std::uint64_t source_handle = source.ffiHandleId();
  auto observer = std::make_shared<TestEncodedObserver>();
  source.setEncodedObserver(observer);

  VideoSource moved(std::move(source));
  ASSERT_EQ(moved.ffiHandleId(), source_handle);

  pushEncodedSourceEvent(makeKeyframeRequestedEvent(moved.ffiHandleId()));

  EXPECT_EQ(observer->keyframe_requests, 1);
  EXPECT_EQ(observer->target_bitrate_changes, 0);
}

} // namespace livekit::test
