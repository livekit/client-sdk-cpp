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
#include <livekit/stats.h>

#include <vector>

#include "stats.pb.h"

namespace livekit::test {

TEST(StatsTest, ScalarFromProtoOverloads) {
  proto::RtcStatsData data;
  proto::CodecStats codec;
  proto::RtpStreamStats rtp;
  proto::ReceivedRtpStreamStats received;
  proto::InboundRtpStreamStats inbound;
  proto::SentRtpStreamStats sent;
  proto::OutboundRtpStreamStats outbound;
  proto::RemoteInboundRtpStreamStats remote_inbound;
  proto::RemoteOutboundRtpStreamStats remote_outbound;
  proto::MediaSourceStats media_source;
  proto::AudioSourceStats audio_source;
  proto::VideoSourceStats video_source;
  proto::AudioPlayoutStats audio_playout;
  proto::PeerConnectionStats peer;
  proto::DataChannelStats data_channel;
  proto::TransportStats transport;
  proto::CandidatePairStats candidate_pair;
  proto::IceCandidateStats ice_candidate;
  proto::CertificateStats certificate;
  proto::StreamStats stream;

  (void)fromProto(data);
  (void)fromProto(codec);
  (void)fromProto(rtp);
  (void)fromProto(received);
  (void)fromProto(inbound);
  (void)fromProto(sent);
  (void)fromProto(outbound);
  (void)fromProto(remote_inbound);
  (void)fromProto(remote_outbound);
  (void)fromProto(media_source);
  (void)fromProto(audio_source);
  (void)fromProto(video_source);
  (void)fromProto(audio_playout);
  (void)fromProto(peer);
  (void)fromProto(data_channel);
  (void)fromProto(transport);
  (void)fromProto(candidate_pair);
  (void)fromProto(ice_candidate);
  (void)fromProto(certificate);
  (void)fromProto(stream);
}

TEST(StatsTest, HighLevelRtcStatsFromProto) {
  proto::RtcStats rtc;
  RtcStats stats = fromProto(rtc);
  (void)stats;
}

TEST(StatsTest, RepeatedRtcStatsFromProto) {
  std::vector<proto::RtcStats> protos(2);
  std::vector<RtcStats> stats = fromProto(protos);
  EXPECT_EQ(stats.size(), 2u);
}

} // namespace livekit::test
