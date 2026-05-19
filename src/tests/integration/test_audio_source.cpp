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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace livekit::test {

namespace {

int envInt(const char* name, int fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }

  try {
    return std::stoi(value);
  } catch (...) {
    return fallback;
  }
}

std::string envString(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? std::string() : std::string(value);
}

int tenMillisecondFrameBytes(int sample_rate, int num_channels) {
  return (sample_rate / 100) * num_channels * static_cast<int>(sizeof(std::int16_t));
}

unsigned long currentProcessId() {
#ifdef _WIN32
  return static_cast<unsigned long>(GetCurrentProcessId());
#else
  return static_cast<unsigned long>(getpid());
#endif
}

AudioFrame createLeakReproFrame(int sample_rate, int num_channels, int frame_bytes) {
  const std::size_t bytes_per_sample_frame = static_cast<std::size_t>(num_channels) * sizeof(std::int16_t);
  const std::size_t requested_bytes = static_cast<std::size_t>(frame_bytes);
  const std::size_t samples_per_channel = (requested_bytes + bytes_per_sample_frame - 1) / bytes_per_sample_frame;
  std::vector<std::int16_t> data(samples_per_channel * static_cast<std::size_t>(num_channels), 0);
  return AudioFrame(std::move(data), sample_rate, num_channels, static_cast<int>(samples_per_channel));
}

} // namespace

class AudioSourceTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogSink::kConsole); }
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

TEST_F(AudioSourceTest, DISABLED_CaptureFramePublishPathLeakReproducer) {
  const int sample_rate = envInt("LIVEKIT_AUDIO_LEAK_SAMPLE_RATE", 48000);
  const int num_channels = envInt("LIVEKIT_AUDIO_LEAK_CHANNELS", 2);
  const int queue_size_ms = envInt("LIVEKIT_AUDIO_LEAK_QUEUE_SIZE_MS", 0);
  const int capture_timeout_ms = envInt("LIVEKIT_AUDIO_LEAK_TIMEOUT_MS", 20);
  const int capture_rate_hz = envInt("LIVEKIT_AUDIO_LEAK_CAPTURE_RATE_HZ", 50);
  const int frame_bytes = envInt("LIVEKIT_AUDIO_LEAK_FRAME_BYTES", tenMillisecondFrameBytes(sample_rate, num_channels));
  const bool publish_track = envInt("LIVEKIT_AUDIO_LEAK_PUBLISH_TRACK", 1) != 0;

  ASSERT_GT(sample_rate, 0);
  ASSERT_GT(num_channels, 0);
  ASSERT_GE(queue_size_ms, 0);
  ASSERT_GE(capture_timeout_ms, 0);
  ASSERT_GT(capture_rate_hz, 0);
  ASSERT_GT(frame_bytes, 0);

  auto source = std::make_shared<AudioSource>(sample_rate, num_channels, queue_size_ms);
  std::unique_ptr<Room> room;
  std::shared_ptr<LocalAudioTrack> track;

  if (publish_track) {
    const std::string url = envString("LIVEKIT_URL");
    const std::string token = envString("LIVEKIT_TOKEN_A");
    if (url.empty() || token.empty()) {
      GTEST_SKIP() << "LIVEKIT_URL and LIVEKIT_TOKEN_A are required when LIVEKIT_AUDIO_LEAK_PUBLISH_TRACK=1";
    }

    room = std::make_unique<Room>();
    RoomOptions options;
    options.auto_subscribe = false;
    ASSERT_TRUE(room->Connect(url, token, options)) << "Sender failed to connect";

    track = LocalAudioTrack::createLocalAudioTrack("audio-source-leak-reproducer", source);
    TrackPublishOptions publish_options;
    publish_options.source = TrackSource::SOURCE_MICROPHONE;
    std::shared_ptr<LocalTrackPublication> publication; 
    ASSERT_NO_THROW(publication = room->localParticipant()->publishTrack(track, publish_options));
    ASSERT_NE(publication, nullptr);
  }

  AudioFrame frame = createLeakReproFrame(sample_rate, num_channels, frame_bytes);
  const auto frame_interval = std::chrono::nanoseconds(1000000000LL / capture_rate_hz);
  const auto report_interval_frames = static_cast<std::uint64_t>(capture_rate_hz) * 10ULL;
  auto next_capture = std::chrono::steady_clock::now();
  auto interval_start = next_capture;
  std::uint64_t captured_frames = 0;
  std::uint64_t interval_start_frames = 0;

  std::cout << "AudioSource leak reproducer pid=" << currentProcessId() << " publish_track=" << publish_track
            << " sample_rate=" << sample_rate << " channels=" << num_channels
            << " samples_per_channel=" << frame.samples_per_channel()
            << " frame_bytes=" << frame.total_samples() * sizeof(std::int16_t)
            << " capture_rate_hz=" << capture_rate_hz << " timeout_ms=" << capture_timeout_ms
            << " queue_size_ms=" << queue_size_ms << '\n';

  while (true) {
    source->captureFrame(frame, capture_timeout_ms);
    ++captured_frames;

    if (captured_frames % report_interval_frames == 0) {
      const auto now = std::chrono::steady_clock::now();
      const std::chrono::duration<double> elapsed = now - interval_start;
      const auto interval_frames = captured_frames - interval_start_frames;
      const double actual_hz = elapsed.count() > 0.0 ? static_cast<double>(interval_frames) / elapsed.count() : 0.0;

      std::cout << "captured_frames=" << captured_frames << " actual_hz=" << actual_hz << '\n';
      interval_start = now;
      interval_start_frames = captured_frames;
    }

    next_capture += frame_interval;
    std::this_thread::sleep_until(next_capture);
  }
}

} // namespace livekit::test
