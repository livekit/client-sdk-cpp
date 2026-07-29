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

TEST(CaptureSourceProtoTest, ConvertsDeviceListToPublicTypes) {
  proto::CaptureDeviceList list;
  auto* device = list.add_devices();
  device->set_id("camera-1");
  device->set_name("Front camera");
  device->set_model_id("model-1");
  device->set_manufacturer("LiveKit");
  device->set_formats_complete(true);

  auto* format = device->add_formats();
  format->mutable_resolution()->set_width(1920);
  format->mutable_resolution()->set_height(1080);
  format->set_framerate_fps(30);
  format->set_frame_format(proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_NV12);

  const std::vector<CaptureDeviceInfo> devices = fromProto(list);

  ASSERT_EQ(devices.size(), 1u);
  const CaptureDeviceInfo& converted = devices.front();
  EXPECT_EQ(converted.id, "camera-1");
  EXPECT_EQ(converted.name, "Front camera");
  ASSERT_TRUE(converted.model_id.has_value());
  EXPECT_EQ(*converted.model_id, "model-1");
  ASSERT_TRUE(converted.manufacturer.has_value());
  EXPECT_EQ(*converted.manufacturer, "LiveKit");
  EXPECT_TRUE(converted.formats_complete);
  ASSERT_EQ(converted.formats.size(), 1u);
  EXPECT_EQ(converted.formats.front().resolution.width, 1920);
  EXPECT_EQ(converted.formats.front().resolution.height, 1080);
  EXPECT_EQ(converted.formats.front().framerate_fps, 30u);
  EXPECT_EQ(converted.formats.front().frame_format, DeviceFrameFormat::NV12);
}

} // namespace
} // namespace livekit::test
