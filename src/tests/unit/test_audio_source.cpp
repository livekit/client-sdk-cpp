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

#include <livekit/audio_source.h>
#include <livekit/livekit.h>

namespace livekit::test {

class AudioSourceTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole); }
  void TearDown() override { livekit::shutdown(); }
};

TEST_F(AudioSourceTest, ConstructAndQueryProperties) {
  AudioSource source(48000, 1);
  EXPECT_EQ(source.sample_rate(), 48000);
  EXPECT_EQ(source.num_channels(), 1);
  EXPECT_NE(source.ffi_handle_id(), 0u);
  EXPECT_DOUBLE_EQ(source.queuedDuration(), 0.0);
}

TEST_F(AudioSourceTest, ClearQueueIsSafeOnFreshSource) {
  AudioSource source(48000, 2, /*queue_size_ms=*/0);
  source.clearQueue();
  EXPECT_DOUBLE_EQ(source.queuedDuration(), 0.0);
}

} // namespace livekit::test
