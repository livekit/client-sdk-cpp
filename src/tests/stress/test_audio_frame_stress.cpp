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

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <livekit/audio_frame.h>
#include <livekit/livekit.h>
#include <thread>
#include <vector>

namespace livekit {
namespace test {

class AudioFrameStressTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogSink::kConsole); }

  void TearDown() override { livekit::shutdown(); }
};

// Stress test: Rapid creation and destruction of AudioFrames
TEST_F(AudioFrameStressTest, RapidFrameCreation) {
  const int num_iterations = 10000;
  const int sample_rate = 48000;
  const int num_channels = 2;
  const int samples_per_channel = 960; // 20ms at 48kHz

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < num_iterations; ++i) {
    AudioFrame frame =
        AudioFrame::create(sample_rate, num_channels, samples_per_channel);
    ASSERT_EQ(frame.sample_rate(), sample_rate);
    ASSERT_EQ(frame.num_channels(), num_channels);
    ASSERT_EQ(frame.samples_per_channel(), samples_per_channel);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Created " << num_iterations << " AudioFrames in "
            << duration.count() << "ms"
            << " (" << (num_iterations * 1000.0 / duration.count())
            << " frames/sec)" << std::endl;
}

// Stress test: Large buffer allocation
TEST_F(AudioFrameStressTest, LargeBufferAllocation) {
  const int sample_rate = 48000;
  const int num_channels = 8;            // 7.1 surround
  const int samples_per_channel = 48000; // 1 second of audio

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < 100; ++i) {
    AudioFrame frame =
        AudioFrame::create(sample_rate, num_channels, samples_per_channel);
    ASSERT_EQ(frame.total_samples(),
              static_cast<size_t>(num_channels * samples_per_channel));
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Created 100 large (1 second, 8-channel) AudioFrames in "
            << duration.count() << "ms" << std::endl;
}

// Stress test: Concurrent frame creation from multiple threads
TEST_F(AudioFrameStressTest, ConcurrentFrameCreation) {
  const int num_threads = 8;
  const int frames_per_thread = 1000;
  std::atomic<int> total_frames{0};
  std::vector<std::thread> threads;

  auto start = std::chrono::high_resolution_clock::now();

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&total_frames, frames_per_thread]() {
      for (int i = 0; i < frames_per_thread; ++i) {
        AudioFrame frame = AudioFrame::create(48000, 2, 960);
        if (frame.sample_rate() == 48000) {
          total_frames.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  EXPECT_EQ(total_frames.load(), num_threads * frames_per_thread);

  std::cout << "Created " << total_frames.load() << " AudioFrames across "
            << num_threads << " threads in " << duration.count() << "ms"
            << std::endl;
}

// Stress test: Memory pressure with many simultaneous frames
TEST_F(AudioFrameStressTest, MemoryPressure) {
  const int num_frames = 1000;
  std::vector<AudioFrame> frames;
  frames.reserve(num_frames);

  auto start = std::chrono::high_resolution_clock::now();

  // Create many frames and keep them alive
  for (int i = 0; i < num_frames; ++i) {
    frames.push_back(AudioFrame::create(48000, 2, 960));
  }

  // Verify all frames are valid
  for (const auto &frame : frames) {
    ASSERT_EQ(frame.sample_rate(), 48000);
    ASSERT_EQ(frame.num_channels(), 2);
    ASSERT_EQ(frame.samples_per_channel(), 960);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Held " << num_frames << " AudioFrames simultaneously in "
            << duration.count() << "ms" << std::endl;

  // Frames are destroyed when vector goes out of scope
}

// Stress test: Data modification under load
TEST_F(AudioFrameStressTest, DataModificationUnderLoad) {
  const int num_frames = 100;
  const int modifications_per_frame = 100;

  auto start = std::chrono::high_resolution_clock::now();

  for (int f = 0; f < num_frames; ++f) {
    AudioFrame frame = AudioFrame::create(48000, 2, 960);
    auto &data = frame.data();

    for (int m = 0; m < modifications_per_frame; ++m) {
      // Simulate audio processing
      for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<int16_t>((data[i] * 2 + m) % 32767);
      }
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Modified " << num_frames << " frames "
            << modifications_per_frame << " times each in " << duration.count()
            << "ms" << std::endl;
}

// Stress test: Copy operations
TEST_F(AudioFrameStressTest, CopyOperationsStress) {
  const int num_copies = 1000;
  std::vector<int16_t> original_data(1920, 12345);
  AudioFrame original(original_data, 48000, 2, 960);

  auto start = std::chrono::high_resolution_clock::now();

  std::vector<AudioFrame> copies;
  copies.reserve(num_copies);

  for (int i = 0; i < num_copies; ++i) {
    copies.push_back(original);
  }

  // Verify all copies are independent
  for (auto &copy : copies) {
    ASSERT_EQ(copy.data()[0], 12345);
    copy.data()[0] = 0; // Modify copy
  }

  // Original should be unchanged
  ASSERT_EQ(original.data()[0], 12345);

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Performed " << num_copies << " copy operations in "
            << duration.count() << "ms" << std::endl;
}

// Stress test: Move operations
TEST_F(AudioFrameStressTest, MoveOperationsStress) {
  const int num_moves = 10000;

  auto start = std::chrono::high_resolution_clock::now();

  AudioFrame frame = AudioFrame::create(48000, 2, 960);

  for (int i = 0; i < num_moves; ++i) {
    AudioFrame moved = std::move(frame);
    frame = std::move(moved);
  }

  ASSERT_EQ(frame.sample_rate(), 48000);

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Performed " << num_moves << " move operations in "
            << duration.count() << "ms" << std::endl;
}

// Stress test: Simulated real-time audio processing
TEST_F(AudioFrameStressTest, SimulatedRealtimeProcessing) {
  const int duration_seconds = 1;
  const int sample_rate = 48000;
  const int frame_size_ms = 10;
  const int frames_per_second = 1000 / frame_size_ms;
  const int total_frames = duration_seconds * frames_per_second;
  const int samples_per_frame = sample_rate * frame_size_ms / 1000;

  std::vector<AudioFrame> processed_frames;
  processed_frames.reserve(total_frames);

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < total_frames; ++i) {
    // Simulate receiving audio
    AudioFrame frame = AudioFrame::create(sample_rate, 2, samples_per_frame);

    // Simulate processing (apply simple gain)
    auto &data = frame.data();
    for (size_t j = 0; j < data.size(); ++j) {
      data[j] = static_cast<int16_t>(data[j] * 0.8);
    }

    processed_frames.push_back(std::move(frame));
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  double processing_time_per_frame_us =
      static_cast<double>(duration.count()) / total_frames;
  double available_time_per_frame_us = frame_size_ms * 1000.0;

  std::cout << "Processed " << total_frames << " frames (" << duration_seconds
            << "s of audio)" << std::endl;
  std::cout << "Average processing time per frame: "
            << processing_time_per_frame_us << "us" << std::endl;
  std::cout << "Available time per frame: " << available_time_per_frame_us
            << "us" << std::endl;
  std::cout << "Processing overhead: "
            << (processing_time_per_frame_us / available_time_per_frame_us *
                100)
            << "%" << std::endl;

  // Processing should be fast enough for real-time
  EXPECT_LT(processing_time_per_frame_us, available_time_per_frame_us)
      << "Processing takes longer than real-time allows";
}

} // namespace test
} // namespace livekit
