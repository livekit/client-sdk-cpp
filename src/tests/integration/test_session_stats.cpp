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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../common/audio_utils.h"
#include "../common/test_common.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr int kAudioSampleRate = kDefaultAudioSampleRate;
constexpr int kAudioChannels = kDefaultAudioChannels;

/// Time to let media flow before sampling stats; below this the RTP counters
/// are typically empty and the printed output is uninteresting.
constexpr auto kStatsWarmup = 5s;

const char* rtcStatsTypeName(const RtcStats& s) {
  return std::visit(
      [](const auto& v) -> const char* {
        using T = std::decay_t<decltype(v)>;
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
      s.stats);
}

void dumpInterestingEntries(const std::vector<RtcStats>& stats) {
  for (const auto& stat : stats) {
    std::visit(
        [&](const auto& s) {
          using T = std::decay_t<decltype(s)>;
          if constexpr (std::is_same_v<T, RtcOutboundRtpStats>) {
            std::cout << "      [OutboundRtp] id=" << s.rtc.id << " kind=" << s.stream.kind
                      << " packets_sent=" << s.sent.packets_sent << " bytes_sent=" << s.sent.bytes_sent
                      << " target_bitrate=" << std::fixed << std::setprecision(2) << s.outbound.target_bitrate
                      << std::endl;
          } else if constexpr (std::is_same_v<T, RtcInboundRtpStats>) {
            std::cout << "      [InboundRtp] id=" << s.rtc.id << " kind=" << s.stream.kind
                      << " packets_received=" << s.received.packets_received
                      << " packets_lost=" << s.received.packets_lost << " jitter=" << std::fixed << std::setprecision(6)
                      << s.received.jitter << " bytes_received=" << s.inbound.bytes_received << std::endl;
          } else if constexpr (std::is_same_v<T, RtcCandidatePairStats>) {
            std::cout << "      [CandidatePair] id=" << s.rtc.id << " rtt=" << std::fixed << std::setprecision(4)
                      << s.candidate_pair.current_round_trip_time << "s"
                      << " in_bitrate=" << s.candidate_pair.available_incoming_bitrate
                      << " out_bitrate=" << s.candidate_pair.available_outgoing_bitrate
                      << " bytes_sent=" << s.candidate_pair.bytes_sent
                      << " bytes_received=" << s.candidate_pair.bytes_received << std::endl;
          } else if constexpr (std::is_same_v<T, RtcTransportStats>) {
            std::cout << "      [Transport] id=" << s.rtc.id << " packets_sent=" << s.transport.packets_sent
                      << " packets_received=" << s.transport.packets_received
                      << " bytes_sent=" << s.transport.bytes_sent << " bytes_received=" << s.transport.bytes_received
                      << std::endl;
          } else if constexpr (std::is_same_v<T, RtcPeerConnectionStats>) {
            std::cout << "      [PeerConnection] id=" << s.rtc.id
                      << " data_channels_opened=" << s.pc.data_channels_opened
                      << " data_channels_closed=" << s.pc.data_channels_closed << std::endl;
          }
        },
        stat.stats);
  }
}

void printSide(const std::string& side_label, const std::vector<RtcStats>& stats) {
  std::cout << "    " << side_label << " entries=" << stats.size();
  std::map<std::string, int> type_counts;
  for (const auto& s : stats) {
    type_counts[rtcStatsTypeName(s)]++;
  }
  if (!type_counts.empty()) {
    std::cout << "   types:";
    for (const auto& kv : type_counts) {
      std::cout << " " << kv.first << "=" << kv.second;
    }
  }
  std::cout << std::endl;
  dumpInterestingEntries(stats);
}

void printSessionStats(const std::string& room_label, const SessionStats& stats) {
  std::cout << "[SessionStats] " << room_label << ":" << std::endl;
  printSide("publisher", stats.publisher_stats);
  printSide("subscriber", stats.subscriber_stats);
}

} // namespace

class SessionStatsIntegrationTest : public LiveKitTestBase {};

TEST_F(SessionStatsIntegrationTest, PublishAudioThenFetchSessionStats) {
  skipIfNotConfigured();

  RoomOptions options;
  options.auto_subscribe = true;
  options.single_peer_connection = false;

  auto receiver_room = std::make_unique<Room>();
  ASSERT_TRUE(receiver_room->connect(config_.url, config_.token_b, options)) << "Receiver failed to connect";

  auto sender_room = std::make_unique<Room>();
  ASSERT_TRUE(sender_room->connect(config_.url, config_.token_a, options)) << "Sender failed to connect";

  auto source = std::make_shared<AudioSource>(kAudioSampleRate, kAudioChannels, 0);
  auto track = LocalAudioTrack::createLocalAudioTrack("session-stats-audio", source);
  TrackPublishOptions opts;
  opts.source = TrackSource::SOURCE_MICROPHONE;
  lockLocalParticipant(*sender_room)->publishTrack(track, opts);
  std::cerr << "[SessionStats] published audio track sid=" << track->sid() << std::endl;

  std::atomic<bool> running{true};
  std::thread audio_thread([&]() { runToneLoop(source, running, /*base_freq_hz=*/440.0, /*siren_mode=*/false); });

  std::this_thread::sleep_for(kStatsWarmup);

  auto sender_fut = sender_room->getStats();
  auto receiver_fut = receiver_room->getStats();

  SessionStats sender_stats;
  SessionStats receiver_stats;
  ASSERT_NO_THROW(sender_stats = sender_fut.get()) << "Sender getStats threw";
  ASSERT_NO_THROW(receiver_stats = receiver_fut.get()) << "Receiver getStats threw";

  running.store(false, std::memory_order_relaxed);
  if (audio_thread.joinable()) {
    audio_thread.join();
  }
  if (track->publication()) {
    lockLocalParticipant(*sender_room)->unpublishTrack(track->publication()->sid());
  }

  printSessionStats("sender", sender_stats);
  printSessionStats("receiver", receiver_stats);

  EXPECT_FALSE(sender_stats.publisher_stats.empty()) << "Sender should have publisher stats";
  EXPECT_FALSE(receiver_stats.subscriber_stats.empty()) << "Receiver should have subscriber stats";
}

TEST_F(SessionStatsIntegrationTest, NotConnectedThrows) {
  Room room;
  EXPECT_THROW(room.getStats(), std::runtime_error);
}

} // namespace livekit::test
