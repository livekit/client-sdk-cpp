/*
 * Copyright 2025 LiveKit
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
#include <livekit/audio_frame.h>
#include <livekit/livekit.h>

namespace livekit {
namespace test {

class AudioFrameTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogSink::kConsole); }

  void TearDown() override { livekit::shutdown(); }
};

TEST_F(AudioFrameTest, CreateWithValidData) {
  std::vector<std::int16_t> data(960, 0); // 10ms at 48kHz mono
  AudioFrame frame(data, 48000, 1, 960);

  EXPECT_EQ(frame.sample_rate(), 48000);
  EXPECT_EQ(frame.num_channels(), 1);
  EXPECT_EQ(frame.samples_per_channel(), 960);
  EXPECT_EQ(frame.total_samples(), 960);
}

TEST_F(AudioFrameTest, CreateStereoFrame) {
  std::vector<std::int16_t> data(1920, 0); // 10ms at 48kHz stereo
  AudioFrame frame(data, 48000, 2, 960);

  EXPECT_EQ(frame.sample_rate(), 48000);
  EXPECT_EQ(frame.num_channels(), 2);
  EXPECT_EQ(frame.samples_per_channel(), 960);
  EXPECT_EQ(frame.total_samples(), 1920);
}

TEST_F(AudioFrameTest, CreateUsingStaticMethod) {
  AudioFrame frame = AudioFrame::create(48000, 2, 960);

  EXPECT_EQ(frame.sample_rate(), 48000);
  EXPECT_EQ(frame.num_channels(), 2);
  EXPECT_EQ(frame.samples_per_channel(), 960);
  EXPECT_EQ(frame.total_samples(), 1920);

  // Created frame should be zero-initialized
  const auto &samples = frame.data();
  for (const auto &sample : samples) {
    EXPECT_EQ(sample, 0);
  }
}

TEST_F(AudioFrameTest, Duration10ms) {
  AudioFrame frame = AudioFrame::create(48000, 1, 480);
  EXPECT_DOUBLE_EQ(frame.duration(), 0.01); // 10ms
}

TEST_F(AudioFrameTest, Duration20ms) {
  AudioFrame frame = AudioFrame::create(48000, 1, 960);
  EXPECT_DOUBLE_EQ(frame.duration(), 0.02); // 20ms
}

TEST_F(AudioFrameTest, DurationVarious) {
  // 16kHz sample rate, 160 samples = 10ms
  AudioFrame frame16k = AudioFrame::create(16000, 1, 160);
  EXPECT_DOUBLE_EQ(frame16k.duration(), 0.01);

  // 44.1kHz sample rate, 441 samples = 10ms
  AudioFrame frame44k = AudioFrame::create(44100, 1, 441);
  EXPECT_DOUBLE_EQ(frame44k.duration(), 0.01);
}

TEST_F(AudioFrameTest, DataAccessMutable) {
  AudioFrame frame = AudioFrame::create(48000, 1, 480);

  // Modify data
  auto &data = frame.data();
  data[0] = 1000;
  data[1] = -1000;

  // Verify changes persisted
  EXPECT_EQ(frame.data()[0], 1000);
  EXPECT_EQ(frame.data()[1], -1000);
}

TEST_F(AudioFrameTest, DataAccessConst) {
  std::vector<std::int16_t> original_data = {100, 200, 300, 400};
  AudioFrame frame(original_data, 48000, 1, 4);

  const AudioFrame &const_frame = frame;
  const auto &data = const_frame.data();

  EXPECT_EQ(data[0], 100);
  EXPECT_EQ(data[1], 200);
  EXPECT_EQ(data[2], 300);
  EXPECT_EQ(data[3], 400);
}

TEST_F(AudioFrameTest, ToString) {
  AudioFrame frame = AudioFrame::create(48000, 2, 960);
  std::string desc = frame.to_string();

  // Should contain relevant info
  EXPECT_FALSE(desc.empty());
  EXPECT_NE(desc.find("48000"), std::string::npos);
}

TEST_F(AudioFrameTest, DefaultConstructor) {
  AudioFrame frame;

  // Default constructed frame should have zero values
  EXPECT_EQ(frame.sample_rate(), 0);
  EXPECT_EQ(frame.num_channels(), 0);
  EXPECT_EQ(frame.samples_per_channel(), 0);
  EXPECT_TRUE(frame.data().empty());
}

TEST_F(AudioFrameTest, CopySemantics) {
  std::vector<std::int16_t> data = {1, 2, 3, 4};
  AudioFrame original(data, 48000, 1, 4);

  AudioFrame copy = original;

  EXPECT_EQ(copy.sample_rate(), original.sample_rate());
  EXPECT_EQ(copy.num_channels(), original.num_channels());
  EXPECT_EQ(copy.samples_per_channel(), original.samples_per_channel());
  EXPECT_EQ(copy.data(), original.data());

  // Modifying copy should not affect original
  copy.data()[0] = 999;
  EXPECT_EQ(original.data()[0], 1);
  EXPECT_EQ(copy.data()[0], 999);
}

TEST_F(AudioFrameTest, MoveSemantics) {
  std::vector<std::int16_t> data = {1, 2, 3, 4};
  AudioFrame original(data, 48000, 1, 4);

  AudioFrame moved = std::move(original);

  EXPECT_EQ(moved.sample_rate(), 48000);
  EXPECT_EQ(moved.num_channels(), 1);
  EXPECT_EQ(moved.samples_per_channel(), 4);
  EXPECT_EQ(moved.data().size(), 4);
}

TEST_F(AudioFrameTest, InvalidDataSizeThrows) {
  // Data size doesn't match num_channels * samples_per_channel
  std::vector<std::int16_t> data(100, 0);

  EXPECT_THROW(AudioFrame(data, 48000, 2, 960), std::invalid_argument);
}

} // namespace test
} // namespace livekit
