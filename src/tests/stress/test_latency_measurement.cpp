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

#include "../common/test_common.h"
#include <cmath>
#include <condition_variable>

namespace livekit {
namespace test {

// Audio configuration for latency test
constexpr int kAudioSampleRate = 48000;
constexpr int kAudioChannels = 1;
constexpr int kAudioFrameDurationMs = 10;
constexpr int kSamplesPerFrame =
    kAudioSampleRate * kAudioFrameDurationMs / 1000;

// Energy threshold for detecting high-energy frames
constexpr double kHighEnergyThreshold = 0.3;

// Number of consecutive high-energy frames to send per pulse
// (helps survive WebRTC audio processing smoothing)
constexpr int kHighEnergyFramesPerPulse = 5;

// =============================================================================
// Audio Helper Functions
// =============================================================================

/// Calculate RMS energy of audio samples (normalized to [-1, 1] range)
static double calculateEnergy(const std::vector<int16_t> &samples) {
  if (samples.empty())
    return 0.0;
  double sum_squared = 0.0;
  for (int16_t sample : samples) {
    double normalized = static_cast<double>(sample) / 32768.0;
    sum_squared += normalized * normalized;
  }
  return std::sqrt(sum_squared / samples.size());
}

/// Generate a high-energy audio frame (sine wave at max amplitude)
static std::vector<int16_t> generateHighEnergyFrame(int samples_per_channel) {
  std::vector<int16_t> data(samples_per_channel * kAudioChannels);
  const double frequency = 1000.0;  // 1kHz sine wave
  const double amplitude = 30000.0; // Near max for int16
  for (int i = 0; i < samples_per_channel; ++i) {
    double t = static_cast<double>(i) / kAudioSampleRate;
    int16_t sample =
        static_cast<int16_t>(amplitude * std::sin(2.0 * M_PI * frequency * t));
    for (int ch = 0; ch < kAudioChannels; ++ch) {
      data[i * kAudioChannels + ch] = sample;
    }
  }
  return data;
}

/// Generate a low-energy (silent) audio frame
static std::vector<int16_t> generateSilentFrame(int samples_per_channel) {
  return std::vector<int16_t>(samples_per_channel * kAudioChannels, 0);
}

// =============================================================================
// Test Fixture
// =============================================================================

class LatencyMeasurementTest : public LiveKitTestBase {};

// =============================================================================
// Test 1: Connection Time Measurement
// =============================================================================
TEST_F(LatencyMeasurementTest, ConnectionTime) {
  skipIfNotConfigured();

  std::cout << "\n=== Connection Time Measurement Test ===" << std::endl;
  std::cout << "Iterations: " << config_.test_iterations << std::endl;

  LatencyStats stats;
  RoomOptions options;
  options.auto_subscribe = true;

  for (int i = 0; i < config_.test_iterations; ++i) {
    auto room = std::make_unique<Room>();

    auto start = std::chrono::high_resolution_clock::now();
    bool connected = room->Connect(config_.url, config_.caller_token, options);
    auto end = std::chrono::high_resolution_clock::now();

    if (connected) {
      double latency_ms =
          std::chrono::duration<double, std::milli>(end - start).count();
      stats.addMeasurement(latency_ms);
      std::cout << "  Iteration " << (i + 1) << ": " << std::fixed
                << std::setprecision(2) << latency_ms << " ms" << std::endl;
    } else {
      std::cout << "  Iteration " << (i + 1) << ": FAILED to connect"
                << std::endl;
    }

    // Small delay between iterations to allow cleanup
    std::this_thread::sleep_for(500ms);
  }

  stats.printStats("Connection Time Statistics");

  EXPECT_GT(stats.count(), 0) << "At least one connection should succeed";
}

// =============================================================================
// Test 2: Audio Latency Measurement using Energy Detection
// =============================================================================
class AudioLatencyDelegate : public RoomDelegate {
public:
  void onTrackSubscribed(Room &, const TrackSubscribedEvent &event) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (event.track && event.track->kind() == TrackKind::KIND_AUDIO) {
      subscribed_audio_track_ = event.track;
      track_cv_.notify_all();
    }
  }

  std::shared_ptr<Track> waitForAudioTrack(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (track_cv_.wait_for(lock, timeout, [this] {
          return subscribed_audio_track_ != nullptr;
        })) {
      return subscribed_audio_track_;
    }
    return nullptr;
  }

private:
  std::mutex mutex_;
  std::condition_variable track_cv_;
  std::shared_ptr<Track> subscribed_audio_track_;
};

TEST_F(LatencyMeasurementTest, AudioLatency) {
  skipIfNotConfigured();

  std::cout << "\n=== Audio Latency Measurement Test ===" << std::endl;
  std::cout << "Using energy detection to measure audio round-trip latency"
            << std::endl;

  // Create receiver room with delegate
  auto receiver_room = std::make_unique<Room>();
  AudioLatencyDelegate receiver_delegate;
  receiver_room->setDelegate(&receiver_delegate);

  RoomOptions options;
  options.auto_subscribe = true;

  bool receiver_connected =
      receiver_room->Connect(config_.url, config_.receiver_token, options);
  ASSERT_TRUE(receiver_connected) << "Receiver failed to connect";

  std::string receiver_identity = receiver_room->localParticipant()->identity();
  std::cout << "Receiver connected as: " << receiver_identity << std::endl;

  // Create sender room (using caller_token)
  auto sender_room = std::make_unique<Room>();
  bool sender_connected =
      sender_room->Connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(sender_connected) << "Sender failed to connect";

  std::string sender_identity = sender_room->localParticipant()->identity();
  std::cout << "Sender connected as: " << sender_identity << std::endl;

  // Wait for sender to be visible to receiver
  ASSERT_TRUE(waitForParticipant(receiver_room.get(), sender_identity, 10s))
      << "Sender not visible to receiver";

  // Create audio source in real-time mode (queue_size_ms = 0)
  // We'll pace the frames ourselves to match real-time delivery
  auto audio_source =
      std::make_shared<AudioSource>(kAudioSampleRate, kAudioChannels, 0);
  auto audio_track =
      LocalAudioTrack::createLocalAudioTrack("latency-test", audio_source);

  TrackPublishOptions publish_options;
  auto publication = sender_room->localParticipant()->publishTrack(
      audio_track, publish_options);
  ASSERT_NE(publication, nullptr) << "Failed to publish audio track";

  std::cout << "Audio track published, waiting for subscription..."
            << std::endl;

  // Wait for receiver to subscribe to the audio track
  auto subscribed_track = receiver_delegate.waitForAudioTrack(10s);
  ASSERT_NE(subscribed_track, nullptr)
      << "Receiver did not subscribe to audio track";

  std::cout << "Audio track subscribed, creating audio stream..." << std::endl;

  // Create audio stream from the subscribed track
  AudioStream::Options stream_options;
  stream_options.capacity = 100; // Small buffer to reduce latency
  auto audio_stream = AudioStream::fromTrack(subscribed_track, stream_options);
  ASSERT_NE(audio_stream, nullptr) << "Failed to create audio stream";

  // Statistics for latency measurements
  LatencyStats stats;
  std::atomic<bool> running{true};
  std::atomic<uint64_t> last_high_energy_send_time_us{0};
  std::atomic<bool> waiting_for_echo{false};
  std::atomic<int> missed_pulses{0};

  // Timeout for waiting for echo (2 seconds)
  constexpr uint64_t kEchoTimeoutUs = 2000000;

  // Receiver thread: detect high energy frames and calculate latency
  std::thread receiver_thread([&]() {
    AudioFrameEvent event;
    while (running.load() && audio_stream->read(event)) {
      double energy = calculateEnergy(event.frame.data());

      if (waiting_for_echo.load() && energy > kHighEnergyThreshold) {
        uint64_t receive_time_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        uint64_t send_time_us = last_high_energy_send_time_us.load();

        if (send_time_us > 0) {
          double latency_ms = (receive_time_us - send_time_us) / 1000.0;
          if (latency_ms > 0 && latency_ms < 5000) { // Sanity check
            stats.addMeasurement(latency_ms);
            std::cout << "  Audio latency: " << std::fixed
                      << std::setprecision(2) << latency_ms << " ms"
                      << " (energy: " << std::setprecision(3) << energy << ")"
                      << std::endl;
          }
          waiting_for_echo.store(false);
        }
      }
    }
  });

  // Sender thread: send audio frames in real-time (10ms audio every 10ms)
  // Hijack periodic frames with high energy for latency measurement
  std::thread sender_thread([&]() {
    int frame_count = 0;
    const int frames_between_pulses =
        100; // Send pulse every 100 frames (~1 second)
    const int total_pulses = 10;
    int pulses_sent = 0;
    uint64_t pulse_send_time = 0;
    int high_energy_frames_remaining =
        0; // Counter for consecutive high-energy frames

    // Use steady timing to maintain real-time pace
    auto next_frame_time = std::chrono::steady_clock::now();
    const auto frame_duration =
        std::chrono::milliseconds(kAudioFrameDurationMs);

    while (running.load() && pulses_sent < total_pulses) {
      // Wait until it's time to send the next frame (real-time pacing)
      std::this_thread::sleep_until(next_frame_time);
      next_frame_time += frame_duration;

      std::vector<int16_t> frame_data;

      // Check for echo timeout
      if (waiting_for_echo.load() && pulse_send_time > 0) {
        uint64_t now_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        if (now_us - pulse_send_time > kEchoTimeoutUs) {
          std::cout << "  Echo timeout for pulse " << pulses_sent
                    << ", moving on..." << std::endl;
          waiting_for_echo.store(false);
          missed_pulses++;
          pulse_send_time = 0;
          high_energy_frames_remaining = 0;
        }
      }

      // Continue sending high-energy frames if we're in the middle of a pulse
      if (high_energy_frames_remaining > 0) {
        frame_data = generateHighEnergyFrame(kSamplesPerFrame);
        high_energy_frames_remaining--;
      } else if (frame_count % frames_between_pulses == 0 &&
                 !waiting_for_echo.load()) {
        // Start a new pulse - send multiple consecutive high-energy frames
        frame_data = generateHighEnergyFrame(kSamplesPerFrame);
        high_energy_frames_remaining =
            kHighEnergyFramesPerPulse - 1; // -1 because we're sending one now

        pulse_send_time =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        last_high_energy_send_time_us.store(pulse_send_time);
        waiting_for_echo.store(true);
        pulses_sent++;

        std::cout << "Sent pulse " << pulses_sent << "/" << total_pulses << " ("
                  << kHighEnergyFramesPerPulse << " frames)" << std::endl;
      } else {
        // Send silence (but still real audio frames for proper timing)
        frame_data = generateSilentFrame(kSamplesPerFrame);
      }

      AudioFrame frame(std::move(frame_data), kAudioSampleRate, kAudioChannels,
                       kSamplesPerFrame);

      try {
        audio_source->captureFrame(frame);
      } catch (const std::exception &e) {
        std::cerr << "Error capturing frame: " << e.what() << std::endl;
      }

      frame_count++;
    }

    // Wait a bit for last echo to arrive
    std::this_thread::sleep_for(2s);
    running.store(false);
  });

  // Wait for threads to complete
  sender_thread.join();
  audio_stream->close();
  receiver_thread.join();

  stats.printStats("Audio Latency Statistics");

  if (missed_pulses > 0) {
    std::cout << "Missed pulses (timeout): " << missed_pulses << std::endl;
  }

  // Clean up
  sender_room->localParticipant()->unpublishTrack(publication->sid());

  EXPECT_GT(stats.count(), 0)
      << "At least one audio latency measurement should be recorded";
}

} // namespace test
} // namespace livekit
