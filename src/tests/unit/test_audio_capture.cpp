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
#include <livekit/capture/audio_source.h>
#include <livekit/livekit.h>

#include <chrono>

namespace livekit::test {
namespace {

using namespace std::chrono_literals;

class GstreamerAudioTest : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(initialize(LogLevel::Warn)); }
  void TearDown() override { shutdown(); }
};

TEST_F(GstreamerAudioTest, CapturesSyntheticPcmAndReportsFormatAndStats) {
  capture::GstreamerAudioSourceConfig config;
  config.pipeline =
      "audiotestsrc wave=sine samplesperbuffer=480 num-buffers=8 ! "
      "audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=1 ! "
      "appsink name=lk_audio_appsink";

  auto pending = capture::GstreamerAudioCaptureSource::create(config);
  ASSERT_EQ(pending.wait_for(10s), std::future_status::ready);
  const auto created = pending.get();
  ASSERT_TRUE(created.ok()) << created.error().message;
  const auto source = created.value();
  ASSERT_NE(source, nullptr);
  EXPECT_EQ(source->format().sample_rate, 48000u);
  EXPECT_EQ(source->format().num_channels, 1u);
  EXPECT_EQ(source->format().samples_per_channel, 480u);
  ASSERT_NE(source->audioSource(), nullptr);
  EXPECT_EQ(source->audioSource()->sampleRate(), 48000);
  EXPECT_EQ(source->audioSource()->numChannels(), 1);

  ASSERT_TRUE(source->start().ok());
  auto joined = source->join();
  ASSERT_EQ(joined.wait_for(10s), std::future_status::ready);
  const auto finished = joined.get();
  ASSERT_TRUE(finished.ok()) << finished.error().message;
  EXPECT_EQ(finished.value().exit, capture::CaptureExit::EndOfStream);
  EXPECT_EQ(finished.value().frames_captured, 8u);
}

TEST_F(GstreamerAudioTest, RechunksStereoBuffersIntoTenMsFrames) {
  capture::GstreamerAudioSourceConfig config;
  config.num_channels = 2;
  config.pipeline =
      "audiotestsrc samplesperbuffer=960 num-buffers=4 ! "
      "audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2 ! "
      "appsink name=lk_audio_appsink";

  auto pending = capture::GstreamerAudioCaptureSource::create(config);
  ASSERT_EQ(pending.wait_for(10s), std::future_status::ready);
  const auto created = pending.get();
  ASSERT_TRUE(created.ok()) << created.error().message;
  const auto source = created.value();
  EXPECT_EQ(source->format().num_channels, 2u);
  EXPECT_EQ(source->format().samples_per_channel, 480u);
  ASSERT_TRUE(source->start().ok());
  auto joined = source->join();
  ASSERT_EQ(joined.wait_for(10s), std::future_status::ready);
  const auto finished = joined.get();
  ASSERT_TRUE(finished.ok()) << finished.error().message;
  EXPECT_EQ(finished.value().exit, capture::CaptureExit::EndOfStream);
  EXPECT_EQ(finished.value().frames_captured, 8u);
}

} // namespace
} // namespace livekit::test
