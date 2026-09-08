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
#include <livekit/encoded_video_source.h>
#include <livekit/livekit.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace livekit::test {

class EncodedVideoIngestionStressTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogLevel::Warn); }
  void TearDown() override { livekit::shutdown(); }
};

TEST_F(EncodedVideoIngestionStressTest, MeasuresSustainedFfiSubmission) {
  constexpr std::size_t kPayloadSize = std::size_t{256} * 1024U;
  constexpr std::size_t kFrameCount = 300U;

  const EncodedVideoSource source(VideoCodec::H264, 1920, 1080);
  EncodedVideoSource::Frame frame;
  frame.is_keyframe = true;
  frame.data.assign(kPayloadSize, 0x55);

  std::size_t accepted = 0;
  const auto started_at = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < kFrameCount; ++index) {
    frame.timestamp_us = static_cast<std::int64_t>(index) * 33333;
    if (source.captureFrame(frame)) {
      ++accepted;
    }
  }
  const auto elapsed = std::chrono::steady_clock::now() - started_at;
  const double elapsed_seconds = std::chrono::duration<double>(elapsed).count();
  const double throughput_mib_per_second =
      (static_cast<double>(kPayloadSize * kFrameCount) / (1024.0 * 1024.0)) / elapsed_seconds;

  std::cout << "Pre-encoded FFI submission: " << throughput_mib_per_second << " MiB/s (" << kFrameCount << " frames)\n";
  EXPECT_EQ(accepted, kFrameCount);
  EXPECT_LT(elapsed, std::chrono::seconds(30));
}

} // namespace livekit::test
