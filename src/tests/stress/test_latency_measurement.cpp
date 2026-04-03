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

#include "../benchmark/benchmark_utils.h"
#include "../common/test_common.h"
#include "trace/trace_event.h"
#include <cmath>
#include <condition_variable>
#include <type_traits>
#include <variant>

using namespace livekit::test::benchmark;

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

static const char *rtcStatsTypeName(const RtcStats &stats) {
  return std::visit(
      [](const auto &s) -> const char * {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, RtcCodecStats>) {
          return "Codec";
        } else if constexpr (std::is_same_v<T, RtcInboundRtpStats>) {
          return "InboundRtp";
        } else if constexpr (std::is_same_v<T, RtcOutboundRtpStats>) {
          return "OutboundRtp";
        } else if constexpr (std::is_same_v<T, RtcRemoteInboundRtpStats>) {
          return "RemoteInboundRtp";
        } else if constexpr (std::is_same_v<T, RtcRemoteOutboundRtpStats>) {
          return "RemoteOutboundRtp";
        } else if constexpr (std::is_same_v<T, RtcMediaSourceStats>) {
          return "MediaSource";
        } else if constexpr (std::is_same_v<T, RtcMediaPlayoutStats>) {
          return "MediaPlayout";
        } else if constexpr (std::is_same_v<T, RtcPeerConnectionStats>) {
          return "PeerConnection";
        } else if constexpr (std::is_same_v<T, RtcDataChannelStats>) {
          return "DataChannel";
        } else if constexpr (std::is_same_v<T, RtcTransportStats>) {
          return "Transport";
        } else if constexpr (std::is_same_v<T, RtcCandidatePairStats>) {
          return "CandidatePair";
        } else if constexpr (std::is_same_v<T, RtcLocalCandidateStats>) {
          return "LocalCandidate";
        } else if constexpr (std::is_same_v<T, RtcRemoteCandidateStats>) {
          return "RemoteCandidate";
        } else if constexpr (std::is_same_v<T, RtcCertificateStats>) {
          return "Certificate";
        } else if constexpr (std::is_same_v<T, RtcStreamStats>) {
          return "Stream";
        } else {
          return "Unknown";
        }
      },
      stats.stats);
}

static void printSessionStats(const std::vector<RtcStats> &stats,
                              const std::string &label) {
  std::cout << "    " << label << " stats: " << stats.size() << std::endl;
  for (const auto &stat : stats) {
    std::visit(
        [&](const auto &s) {
          using T = std::decay_t<decltype(s)>;
          if constexpr (std::is_same_v<T, RtcCandidatePairStats>) {
            std::cout << "      [CandidatePair] id=" << s.rtc.id
                      << " rtt=" << std::fixed << std::setprecision(4)
                      << s.candidate_pair.current_round_trip_time << "s"
                      << " total_rtt=" << s.candidate_pair.total_round_trip_time
                      << "s"
                      << " in_bitrate="
                      << s.candidate_pair.available_incoming_bitrate
                      << " out_bitrate="
                      << s.candidate_pair.available_outgoing_bitrate
                      << " bytes_sent=" << s.candidate_pair.bytes_sent
                      << " bytes_received=" << s.candidate_pair.bytes_received
                      << std::endl;
          } else if constexpr (std::is_same_v<T, RtcTransportStats>) {
            std::cout << "      [Transport] id=" << s.rtc.id
                      << " selected_pair="
                      << s.transport.selected_candidate_pair_id
                      << " packets_sent=" << s.transport.packets_sent
                      << " packets_received=" << s.transport.packets_received
                      << " bytes_sent=" << s.transport.bytes_sent
                      << " bytes_received=" << s.transport.bytes_received
                      << std::endl;
          } else if constexpr (std::is_same_v<T, RtcPeerConnectionStats>) {
            std::cout << "      [PeerConnection] id=" << s.rtc.id
                      << " data_channels_opened=" << s.pc.data_channels_opened
                      << " data_channels_closed=" << s.pc.data_channels_closed
                      << std::endl;
          } else if constexpr (std::is_same_v<T, RtcInboundRtpStats>) {
            std::cout << "      [InboundRtp] id=" << s.rtc.id
                      << " kind=" << s.stream.kind
                      << " packets_lost=" << s.received.packets_lost
                      << " jitter=" << std::fixed << std::setprecision(6)
                      << s.received.jitter
                      << " bytes_received=" << s.inbound.bytes_received
                      << std::endl;
          } else if constexpr (std::is_same_v<T, RtcOutboundRtpStats>) {
            std::cout << "      [OutboundRtp] id=" << s.rtc.id
                      << " kind=" << s.stream.kind
                      << " packets_sent=" << s.sent.packets_sent
                      << " bytes_sent=" << s.sent.bytes_sent
                      << " target_bitrate=" << std::fixed
                      << std::setprecision(2) << s.outbound.target_bitrate
                      << std::endl;
          }
        },
        stat.stats);
  }

  std::map<std::string, int> type_counts;
  for (const auto &stat : stats) {
    type_counts[rtcStatsTypeName(stat)]++;
  }
  if (!type_counts.empty()) {
    std::cout << "    " << label << " type counts:";
    for (const auto &kv : type_counts) {
      std::cout << " " << kv.first << "=" << kv.second;
    }
    std::cout << std::endl;
  }
}

static void
printAudioLatencyAndNetworkSummary(const std::vector<RtcStats> &stats,
                                   const std::string &label) {
  std::cout << "    " << label << " audio/network summary:" << std::endl;
  bool printed = false;

  for (const auto &stat : stats) {
    std::visit(
        [&](const auto &s) {
          using T = std::decay_t<decltype(s)>;
          if constexpr (std::is_same_v<T, RtcInboundRtpStats>) {
            if (s.stream.kind == "audio") {
              printed = true;
              double emitted =
                  static_cast<double>(s.inbound.jitter_buffer_emitted_count);
              double avg_jb_delay_s =
                  emitted > 0.0 ? (s.inbound.jitter_buffer_delay / emitted)
                                : 0.0;
              double avg_jb_target_s =
                  emitted > 0.0
                      ? (s.inbound.jitter_buffer_target_delay / emitted)
                      : 0.0;
              double avg_processing_s =
                  emitted > 0.0 ? (s.inbound.total_processing_delay / emitted)
                                : 0.0;

              std::cout << "      [InboundAudio] id=" << s.rtc.id
                        << " packets_received=" << s.received.packets_received
                        << " packets_lost=" << s.received.packets_lost
                        << " jitter=" << std::fixed << std::setprecision(6)
                        << s.received.jitter
                        << " jb_delay_total_s=" << s.inbound.jitter_buffer_delay
                        << " jb_target_total_s="
                        << s.inbound.jitter_buffer_target_delay
                        << " jb_emitted="
                        << s.inbound.jitter_buffer_emitted_count
                        << " jb_delay_avg_ms=" << std::setprecision(2)
                        << (avg_jb_delay_s * 1000.0)
                        << " jb_target_avg_ms=" << (avg_jb_target_s * 1000.0)
                        << " processing_avg_ms=" << (avg_processing_s * 1000.0)
                        << " concealed_samples=" << s.inbound.concealed_samples
                        << " inserted_for_decel="
                        << s.inbound.inserted_samples_for_deceleration
                        << " removed_for_accel="
                        << s.inbound.removed_samples_for_acceleration
                        << std::endl;
            }
          } else if constexpr (std::is_same_v<T, RtcCandidatePairStats>) {
            printed = true;
            std::cout << "      [CandidatePair] id=" << s.rtc.id
                      << " rtt_ms=" << std::fixed << std::setprecision(2)
                      << (s.candidate_pair.current_round_trip_time * 1000.0)
                      << " total_rtt_s=" << std::setprecision(4)
                      << s.candidate_pair.total_round_trip_time
                      << " bytes_sent=" << s.candidate_pair.bytes_sent
                      << " bytes_received=" << s.candidate_pair.bytes_received
                      << " in_bitrate="
                      << s.candidate_pair.available_incoming_bitrate
                      << " out_bitrate="
                      << s.candidate_pair.available_outgoing_bitrate
                      << std::endl;
          } else if constexpr (std::is_same_v<T, RtcTransportStats>) {
            printed = true;
            std::cout << "      [Transport] id=" << s.rtc.id
                      << " selected_pair="
                      << s.transport.selected_candidate_pair_id
                      << " packets_sent=" << s.transport.packets_sent
                      << " packets_received=" << s.transport.packets_received
                      << " bytes_sent=" << s.transport.bytes_sent
                      << " bytes_received=" << s.transport.bytes_received
                      << std::endl;
          }
        },
        stat.stats);
  }

  if (!printed) {
    std::cout << "      (no audio/network stats available)" << std::endl;
  }
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

  RoomOptions options;
  options.auto_subscribe = true;
  int successful_connections = 0;

  for (int i = 0; i < config_.test_iterations; ++i) {
    auto room = std::make_unique<Room>();

    auto start = std::chrono::high_resolution_clock::now();
    // Room::Connect() has built-in TRACE_EVENT0 for automatic timing
    bool connected = room->Connect(config_.url, config_.caller_token, options);
    auto end = std::chrono::high_resolution_clock::now();

    if (connected) {
      successful_connections++;
      double latency_ms =
          std::chrono::duration<double, std::milli>(end - start).count();
      std::cout << "  Iteration " << (i + 1) << ": " << std::fixed
                << std::setprecision(2) << latency_ms << " ms" << std::endl;
    } else {
      std::cout << "  Iteration " << (i + 1) << ": FAILED to connect"
                << std::endl;
    }

    // Small delay between iterations to allow cleanup
    std::this_thread::sleep_for(500ms);
  }

  // Tracing is automatically handled by LiveKitTestBase
  // Stats for Room::Connect will be printed in TearDown()

  EXPECT_GT(successful_connections, 0)
      << "At least one connection should succeed";
}

// =============================================================================
// Test 2: Audio Latency Measurement using Energy Detection
// =============================================================================
class AudioLatencyDelegate : public RoomDelegate {
public:
  void onTrackSubscribed(Room &, const TrackSubscribedEvent &event) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (event.track && event.track->kind() == TrackKind::KIND_AUDIO &&
        event.participant) {
      subscribed_audio_track_ = event.track;
      subscribed_audio_tracks_by_participant_[event.participant->identity()] =
          event.track;
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

  std::shared_ptr<Track>
  waitForAudioTrackFromParticipant(const std::string &identity,
                                   std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (track_cv_.wait_for(lock, timeout, [this, &identity] {
          return subscribed_audio_tracks_by_participant_.count(identity) > 0;
        })) {
      return subscribed_audio_tracks_by_participant_[identity];
    }
    return nullptr;
  }

private:
  std::mutex mutex_;
  std::condition_variable track_cv_;
  std::shared_ptr<Track> subscribed_audio_track_;
  std::map<std::string, std::shared_ptr<Track>>
      subscribed_audio_tracks_by_participant_;
};

TEST_F(LatencyMeasurementTest, AudioLatency) {
  skipIfNotConfigured();

  std::cout << "\n=== Audio Latency Measurement Test ===" << std::endl;
  std::cout << "Using energy detection to measure audio round-trip latency"
            << std::endl;

  // Register custom trace events to analyze at test end
  addTraceEventToAnalyze("audio_latency");

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
  auto audio_source =
      std::make_shared<AudioSource>(kAudioSampleRate, kAudioChannels, 0);
  auto audio_track =
      LocalAudioTrack::createLocalAudioTrack("latency-test", audio_source);

  TrackPublishOptions publish_options;
  sender_room->localParticipant()->publishTrack(audio_track, publish_options);
  ASSERT_NE(audio_track->publication(), nullptr)
      << "Failed to publish audio track";

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

  std::atomic<bool> running{true};
  std::atomic<uint64_t> last_high_energy_send_time_us{0};
  std::atomic<bool> waiting_for_echo{false};
  std::atomic<int> missed_pulses{0};
  std::atomic<int> successful_measurements{0};
  std::atomic<uint64_t> current_pulse_id{0};

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
            // End the async trace span
            TRACE_EVENT_ASYNC_END1(kCategoryAudio, "audio_latency",
                                   current_pulse_id.load(), "latency_ms",
                                   latency_ms);
            successful_measurements++;
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
  std::thread sender_thread([&]() {
    int frame_count = 0;
    const int frames_between_pulses = 100; // ~1 second between pulses
    const int total_pulses = 10;
    int pulses_sent = 0;
    uint64_t pulse_send_time = 0;
    int high_energy_frames_remaining = 0;

    auto next_frame_time = std::chrono::steady_clock::now();
    const auto frame_duration =
        std::chrono::milliseconds(kAudioFrameDurationMs);

    while (running.load() && pulses_sent < total_pulses) {
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

      if (high_energy_frames_remaining > 0) {
        frame_data = generateHighEnergyFrame(kSamplesPerFrame);
        high_energy_frames_remaining--;
      } else if (frame_count % frames_between_pulses == 0 &&
                 !waiting_for_echo.load()) {
        // Start a new pulse
        frame_data = generateHighEnergyFrame(kSamplesPerFrame);
        high_energy_frames_remaining = kHighEnergyFramesPerPulse - 1;

        pulse_send_time =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        last_high_energy_send_time_us.store(pulse_send_time);
        waiting_for_echo.store(true);
        pulses_sent++;

        // Begin async trace span
        current_pulse_id.store(static_cast<uint64_t>(pulses_sent));
        TRACE_EVENT_ASYNC_BEGIN1(kCategoryAudio, "audio_latency",
                                 current_pulse_id.load(), "pulse", pulses_sent);

        std::cout << "Sent pulse " << pulses_sent << "/" << total_pulses << " ("
                  << kHighEnergyFramesPerPulse << " frames)" << std::endl;
      } else {
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

    std::this_thread::sleep_for(2s);
    running.store(false);
  });

  // Wait for threads to complete
  sender_thread.join();
  audio_stream->close();
  receiver_thread.join();

  if (missed_pulses > 0) {
    std::cout << "Missed pulses (timeout): " << missed_pulses << std::endl;
  }

  // Clean up
  sender_room->localParticipant()->unpublishTrack(
      audio_track->publication()->sid());

  // Tracing is automatically handled by LiveKitTestBase
  // Stats for audio_latency will be printed in TearDown()

  EXPECT_GT(successful_measurements.load(), 0)
      << "At least one audio latency measurement should be recorded";
}

TEST_F(LatencyMeasurementTest, FullDeplexAudioLatency) {
  skipIfNotConfigured();

  std::cout << "\n=== FullDeplexAudioLatency Test ===" << std::endl;
  std::cout << "Measuring A->B, B->A, and A->B->A using audio ping-pong"
            << std::endl;

  // Register custom trace events to analyze at test end
  addTraceEventToAnalyze("A_to_B");
  addTraceEventToAnalyze("B_to_A");
  addTraceEventToAnalyze("round_trip");

  auto room_a = std::make_unique<Room>(); // caller
  auto room_b = std::make_unique<Room>(); // receiver
  AudioLatencyDelegate delegate_a;
  AudioLatencyDelegate delegate_b;
  room_a->setDelegate(&delegate_a);
  room_b->setDelegate(&delegate_b);

  RoomOptions options;
  options.auto_subscribe = true;

  ASSERT_TRUE(room_a->Connect(config_.url, config_.caller_token, options))
      << "Participant A failed to connect";
  ASSERT_TRUE(room_b->Connect(config_.url, config_.receiver_token, options))
      << "Participant B failed to connect";

  std::string id_a = room_a->localParticipant()->identity();
  std::string id_b = room_b->localParticipant()->identity();
  std::cout << "Participant A: " << id_a << std::endl;
  std::cout << "Participant B: " << id_b << std::endl;

  ASSERT_TRUE(waitForParticipant(room_a.get(), id_b, 10s)) << "A cannot see B";
  ASSERT_TRUE(waitForParticipant(room_b.get(), id_a, 10s)) << "B cannot see A";

  auto source_a =
      std::make_shared<AudioSource>(kAudioSampleRate, kAudioChannels, 0);
  auto source_b =
      std::make_shared<AudioSource>(kAudioSampleRate, kAudioChannels, 1000);
  auto track_a =
      LocalAudioTrack::createLocalAudioTrack("full-duplex-a", source_a);
  auto track_b =
      LocalAudioTrack::createLocalAudioTrack("full-duplex-b", source_b);
  ASSERT_NE(track_a, nullptr);
  ASSERT_NE(track_b, nullptr);

  TrackPublishOptions publish_options;
  room_a->localParticipant()->publishTrack(track_a, publish_options);
  room_b->localParticipant()->publishTrack(track_b, publish_options);

  auto track_from_a_on_b =
      delegate_b.waitForAudioTrackFromParticipant(id_a, 10s);
  auto track_from_b_on_a =
      delegate_a.waitForAudioTrackFromParticipant(id_b, 10s);
  ASSERT_NE(track_from_a_on_b, nullptr) << "B did not subscribe to A audio";
  ASSERT_NE(track_from_b_on_a, nullptr) << "A did not subscribe to B audio";

  AudioStream::Options stream_options;
  stream_options.capacity = 100;
  auto stream_b_recv_a =
      AudioStream::fromTrack(track_from_a_on_b, stream_options);
  auto stream_a_recv_b =
      AudioStream::fromTrack(track_from_b_on_a, stream_options);
  ASSERT_NE(stream_b_recv_a, nullptr);
  ASSERT_NE(stream_a_recv_b, nullptr);

  std::atomic<bool> running{true};
  std::atomic<int> active_pulse_id{0};
  std::atomic<uint64_t> a_send_us{0};
  std::atomic<uint64_t> b_detect_us{0};
  std::atomic<uint64_t> b_send_us{0};
  std::atomic<int> b_responded_pulse_id{0};
  std::atomic<int> a_received_pulse_id{0};
  std::atomic<bool> waiting_for_response{false};
  std::atomic<int> pre_pulse_silence_frames_remaining{200};
  std::atomic<int> timeouts{0};
  std::atomic<int> a_to_b_count{0};
  std::atomic<int> b_to_a_count{0};
  std::atomic<int> round_trip_count{0};

  constexpr int kTotalPulses = 100;
  constexpr int kPrePulseSilenceFrames = 50;    // 500ms at 10ms/frame
  constexpr uint64_t kPulseTimeoutUs = 8000000; // 8 seconds
  constexpr int kBMaxResponseFrames = 50;       // 500ms at 10ms/frame
  constexpr double kMinValidOneWayMs =
      10.0;                                // Filter impossible matches (< 10ms)
  constexpr double kMinValidBToAMs = 10.0; // Filter impossible matches (< 10ms)

  // B receives A pulses and sends response frames
  std::thread b_receiver_thread([&]() {
    AudioFrameEvent event;
    while (running.load() && stream_b_recv_a->read(event)) {
      if (calculateEnergy(event.frame.data()) <= kHighEnergyThreshold) {
        continue;
      }

      int pulse_id = active_pulse_id.load();
      if (pulse_id <= 0 || b_responded_pulse_id.load() == pulse_id) {
        continue;
      }

      uint64_t detect_us =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count();
      uint64_t send_from_a_us = a_send_us.load();
      if (send_from_a_us == 0 || detect_us <= send_from_a_us) {
        continue;
      }
      double a_to_b_ms = (detect_us - send_from_a_us) / 1000.0;
      if (a_to_b_ms < kMinValidOneWayMs || a_to_b_ms > 5000) {
        continue;
      }

      b_detect_us.store(detect_us);
      b_responded_pulse_id.store(pulse_id);

      // End the A->B trace span
      TRACE_EVENT_ASYNC_END1(kCategoryAudio, "A_to_B",
                             static_cast<uint64_t>(pulse_id), "latency_ms",
                             a_to_b_ms);
      a_to_b_count++;
      std::cout << "  A->B latency: " << std::fixed << std::setprecision(2)
                << a_to_b_ms << " ms" << std::endl;

      // Begin B->A trace span
      TRACE_EVENT_ASYNC_BEGIN1(kCategoryAudio, "B_to_A",
                               static_cast<uint64_t>(pulse_id), "pulse",
                               pulse_id);

      for (int i = 0; i < kBMaxResponseFrames; ++i) {
        std::vector<int16_t> pulse = generateHighEnergyFrame(kSamplesPerFrame);
        AudioFrame response_frame(std::move(pulse), kAudioSampleRate,
                                  kAudioChannels, kSamplesPerFrame);
        try {
          if (i == 0) {
            b_send_us.store(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
          }
          source_b->captureFrame(response_frame);
        } catch (const std::exception &e) {
          std::cerr << "Error sending B response frame: " << e.what()
                    << std::endl;
          break;
        }
        if (!waiting_for_response.load()) {
          break;
        }
        std::this_thread::sleep_for(10ms);
      }
    }
  });

  // A receives B responses and computes B->A and A->B->A
  std::thread a_receiver_thread([&]() {
    AudioFrameEvent event;
    while (running.load() && stream_a_recv_b->read(event)) {
      if (!waiting_for_response.load() ||
          calculateEnergy(event.frame.data()) <= kHighEnergyThreshold) {
        continue;
      }

      int pulse_id = active_pulse_id.load();
      if (pulse_id <= 0 || a_received_pulse_id.load() == pulse_id) {
        continue;
      }

      uint64_t receive_us =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count();
      uint64_t send_from_a_us = a_send_us.load();
      uint64_t send_from_b_us = b_send_us.load();
      int responded_pulse_id = b_responded_pulse_id.load();

      if (responded_pulse_id != pulse_id || send_from_b_us == 0 ||
          receive_us < send_from_b_us) {
        continue;
      }
      a_received_pulse_id.store(pulse_id);

      double b_to_a_ms = (receive_us - send_from_b_us) / 1000.0;
      if (b_to_a_ms >= kMinValidBToAMs && b_to_a_ms < 5000) {
        TRACE_EVENT_ASYNC_END1(kCategoryAudio, "B_to_A",
                               static_cast<uint64_t>(pulse_id), "latency_ms",
                               b_to_a_ms);
        b_to_a_count++;
        std::cout << "  B->A latency: " << std::fixed << std::setprecision(2)
                  << b_to_a_ms << " ms" << std::endl;
      }

      if (send_from_a_us > 0) {
        double rtt_ms = (receive_us - send_from_a_us) / 1000.0;
        if (rtt_ms > 0 && rtt_ms < 10000) {
          TRACE_EVENT_ASYNC_END1(kCategoryAudio, "round_trip",
                                 static_cast<uint64_t>(pulse_id), "latency_ms",
                                 rtt_ms);
          round_trip_count++;
          std::cout << "  A->B->A latency: " << std::fixed
                    << std::setprecision(2) << rtt_ms << " ms" << std::endl;
        }
      }

      waiting_for_response.store(false);
      active_pulse_id.store(0);
      pre_pulse_silence_frames_remaining.store(kPrePulseSilenceFrames);
    }
  });

  // A sends ping pulses
  std::thread a_sender_thread([&]() {
    auto next_frame_time = std::chrono::steady_clock::now();
    const auto frame_duration =
        std::chrono::milliseconds(kAudioFrameDurationMs);
    int pulses_sent = 0;
    uint64_t pulse_start_us = 0;

    while (running.load() &&
           (pulses_sent < kTotalPulses || waiting_for_response.load())) {
      std::this_thread::sleep_until(next_frame_time);
      next_frame_time += frame_duration;

      if (waiting_for_response.load()) {
        uint64_t now_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        if (pulse_start_us > 0 && now_us - pulse_start_us > kPulseTimeoutUs) {
          std::cout << "  Timeout waiting for B response to pulse "
                    << active_pulse_id.load() << std::endl;
          waiting_for_response.store(false);
          active_pulse_id.store(0);
          pre_pulse_silence_frames_remaining.store(kPrePulseSilenceFrames);
          timeouts++;
        }
      }

      std::vector<int16_t> frame_data;
      if (waiting_for_response.load()) {
        frame_data = generateHighEnergyFrame(kSamplesPerFrame);
      } else if (!waiting_for_response.load() && pulses_sent < kTotalPulses) {
        int remaining_silence = pre_pulse_silence_frames_remaining.load();
        if (remaining_silence > 0) {
          pre_pulse_silence_frames_remaining.store(remaining_silence - 1);
          frame_data = generateSilentFrame(kSamplesPerFrame);
        } else {
          pulses_sent++;
          int pulse_id = pulses_sent;
          active_pulse_id.store(pulse_id);
          b_responded_pulse_id.store(0);
          a_received_pulse_id.store(0);
          b_send_us.store(0);

          pulse_start_us =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now().time_since_epoch())
                  .count();
          a_send_us.store(pulse_start_us);
          waiting_for_response.store(true);

          // Begin trace spans for A->B and round-trip
          TRACE_EVENT_ASYNC_BEGIN1(kCategoryAudio, "A_to_B",
                                   static_cast<uint64_t>(pulse_id), "pulse",
                                   pulse_id);
          TRACE_EVENT_ASYNC_BEGIN1(kCategoryAudio, "round_trip",
                                   static_cast<uint64_t>(pulse_id), "pulse",
                                   pulse_id);

          frame_data = generateHighEnergyFrame(kSamplesPerFrame);

          std::cout << "Sent ping pulse " << pulse_id << "/" << kTotalPulses
                    << std::endl;
        }
      } else {
        frame_data = generateSilentFrame(kSamplesPerFrame);
      }

      AudioFrame frame(std::move(frame_data), kAudioSampleRate, kAudioChannels,
                       kSamplesPerFrame);
      try {
        source_a->captureFrame(frame);
      } catch (const std::exception &e) {
        std::cerr << "Error sending A frame: " << e.what() << std::endl;
      }
    }

    std::this_thread::sleep_for(500ms);
    running.store(false);
  });

  a_sender_thread.join();
  stream_a_recv_b->close();
  stream_b_recv_a->close();
  a_receiver_thread.join();
  b_receiver_thread.join();

  if (timeouts > 0) {
    std::cout << "Response timeouts: " << timeouts << std::endl;
  }

  room_a->localParticipant()->unpublishTrack(track_a->publication()->sid());
  room_b->localParticipant()->unpublishTrack(track_b->publication()->sid());

  // Tracing is automatically handled by LiveKitTestBase
  // Stats for A_to_B, B_to_A, round_trip will be printed in TearDown()

  EXPECT_GT(round_trip_count.load(), 0)
      << "At least one round-trip latency measurement should be recorded";
  EXPECT_GT(a_to_b_count.load(), 0)
      << "At least one A->B latency measurement should be recorded";
  EXPECT_GT(b_to_a_count.load(), 0)
      << "At least one B->A latency measurement should be recorded";
}

} // namespace test
} // namespace livekit
