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

#include "capture.pb.h"
#include "capture_source_internal.h"

namespace livekit::test {
namespace {

TEST(CaptureSourceProtoTest, ClockConfigurationUsesMainlineWireContract) {
  ClockVideoSourceConfig config;
  config.resolution = {1920, 1080};
  config.framerate_fps = 30;

  const auto request = toProto(config);
  ASSERT_TRUE(request.IsInitialized());
  ASSERT_TRUE(request.has_clock());
  EXPECT_EQ(request.clock().resolution().width(), 1920u);
  EXPECT_EQ(request.clock().resolution().height(), 1080u);
  EXPECT_EQ(request.clock().framerate_fps(), 30u);
  EXPECT_FALSE(request.has_gstreamer());
  EXPECT_FALSE(request.has_pattern());
}

TEST(CaptureSourceProtoTest, RejectsNonPositiveDimensionsBeforeUnsignedConversion) {
  for (const CaptureResolution resolution : {CaptureResolution{0, 720}, CaptureResolution{-1, 720},
                                             CaptureResolution{1280, 0}, CaptureResolution{1280, -1}}) {
    PatternVideoSourceConfig pattern;
    pattern.resolution = resolution;
    pattern.framerate_fps = 30;
    EXPECT_THROW(toProto(pattern), CaptureSourceError);
    ClockVideoSourceConfig clock;
    clock.resolution = resolution;
    clock.framerate_fps = 30;
    EXPECT_THROW(toProto(clock), CaptureSourceError);
    GstreamerVideoSourceConfig gstreamer;
    gstreamer.resolution = resolution;
    EXPECT_THROW(toProto(gstreamer), CaptureSourceError);
  }
}

TEST(CaptureSourceProtoTest, RejectsZeroFrameRate) {
  PatternVideoSourceConfig pattern;
  pattern.resolution = {1280, 720};
  EXPECT_THROW(toProto(pattern), CaptureSourceError);
  ClockVideoSourceConfig clock;
  clock.resolution = {1280, 720};
  EXPECT_THROW(toProto(clock), CaptureSourceError);
}

TEST(CaptureSourceProtoTest, ValidationErrorsAreDeliveredThroughFuture) {
  std::future<std::shared_ptr<CaptureSource>> future;
  EXPECT_NO_THROW(future = CaptureSource::create(ClockVideoSourceConfig{}));
  ASSERT_TRUE(future.valid());
  EXPECT_THROW(static_cast<void>(future.get()), CaptureSourceError);
}

TEST(CaptureSourceProtoTest, GstreamerCanDiscoverResolution) {
  GstreamerVideoSourceConfig config;
  config.pipeline = "videotestsrc ! vp8enc ! appsink name=lk_appsink";
  const auto request = toProto(config);
  EXPECT_TRUE(request.IsInitialized());
  EXPECT_FALSE(request.gstreamer().has_resolution());
}

} // namespace
} // namespace livekit::test
