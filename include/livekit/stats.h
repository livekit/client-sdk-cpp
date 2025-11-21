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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace livekit {

namespace proto {
class RtcStats;
class RtcStatsData;
class CodecStats;
class RtpStreamStats;
class ReceivedRtpStreamStats;
class InboundRtpStreamStats;
class SentRtpStreamStats;
class OutboundRtpStreamStats;
class RemoteInboundRtpStreamStats;
class RemoteOutboundRtpStreamStats;
class MediaSourceStats;
class AudioSourceStats;
class VideoSourceStats;
class AudioPlayoutStats;
class PeerConnectionStats;
class DataChannelStats;
class TransportStats;
class CandidatePairStats;
class IceCandidateStats;
class CertificateStats;
class StreamStats;
} // namespace proto

// ----------------------
// SDK enums (decoupled from proto enums)
// ----------------------

enum class DataChannelState {
  Connecting,
  Open,
  Closing,
  Closed,
  Unknown,
};

enum class QualityLimitationReason {
  None,
  Cpu,
  Bandwidth,
  Other,
};

enum class IceRole {
  Unknown,
  Controlling,
  Controlled,
};

enum class DtlsTransportState {
  New,
  Connecting,
  Connected,
  Closed,
  Failed,
  Unknown,
};

enum class IceTransportState {
  New,
  Checking,
  Connected,
  Completed,
  Disconnected,
  Failed,
  Closed,
  Unknown,
};

enum class DtlsRole {
  Client,
  Server,
  Unknown,
};

enum class IceCandidatePairState {
  Frozen,
  Waiting,
  InProgress,
  Failed,
  Succeeded,
  Unknown,
};

enum class IceCandidateType {
  Host,
  Srflx,
  Prflx,
  Relay,
  Unknown,
};

enum class IceServerTransportProtocol {
  Udp,
  Tcp,
  Tls,
  Unknown,
};

enum class IceTcpCandidateType {
  Active,
  Passive,
  So,
  Unknown,
};

// ----------------------
// Leaf stats types
// ----------------------

struct RtcStatsData {
  std::string id;
  std::int64_t timestamp_ms;
};

struct CodecStats {
  std::uint32_t payload_type;
  std::string transport_id;
  std::string mime_type;
  std::uint32_t clock_rate;
  std::uint32_t channels;
  std::string sdp_fmtp_line;
};

struct RtpStreamStats {
  std::uint32_t ssrc;
  std::string kind;
  std::string transport_id;
  std::string codec_id;
};

struct ReceivedRtpStreamStats {
  std::uint64_t packets_received;
  std::int64_t packets_lost;
  double jitter;
};

struct InboundRtpStreamStats {
  std::string track_identifier;
  std::string mid;
  std::string remote_id;
  std::uint32_t frames_decoded;
  std::uint32_t key_frames_decoded;
  std::uint32_t frames_rendered;
  std::uint32_t frames_dropped;
  std::uint32_t frame_width;
  std::uint32_t frame_height;
  double frames_per_second;
  std::uint64_t qp_sum;
  double total_decode_time;
  double total_inter_frame_delay;
  double total_squared_inter_frame_delay;
  std::uint32_t pause_count;
  double total_pause_duration;
  std::uint32_t freeze_count;
  double total_freeze_duration;
  double last_packet_received_timestamp;
  std::uint64_t header_bytes_received;
  std::uint64_t packets_discarded;
  std::uint64_t fec_bytes_received;
  std::uint64_t fec_packets_received;
  std::uint64_t fec_packets_discarded;
  std::uint64_t bytes_received;
  std::uint32_t nack_count;
  std::uint32_t fir_count;
  std::uint32_t pli_count;
  double total_processing_delay;
  double estimated_playout_timestamp;
  double jitter_buffer_delay;
  double jitter_buffer_target_delay;
  std::uint64_t jitter_buffer_emitted_count;
  double jitter_buffer_minimum_delay;
  std::uint64_t total_samples_received;
  std::uint64_t concealed_samples;
  std::uint64_t silent_concealed_samples;
  std::uint64_t concealment_events;
  std::uint64_t inserted_samples_for_deceleration;
  std::uint64_t removed_samples_for_acceleration;
  double audio_level;
  double total_audio_energy;
  double total_samples_duration;
  std::uint64_t frames_received;
  std::string decoder_implementation;
  std::string playout_id;
  bool power_efficient_decoder;
  std::uint64_t frames_assembled_from_multiple_packets;
  double total_assembly_time;
  std::uint64_t retransmitted_packets_received;
  std::uint64_t retransmitted_bytes_received;
  std::uint32_t rtx_ssrc;
  std::uint32_t fec_ssrc;
};

struct SentRtpStreamStats {
  std::uint64_t packets_sent;
  std::uint64_t bytes_sent;
};

struct OutboundRtpStreamStats {
  std::string mid;
  std::string media_source_id;
  std::string remote_id;
  std::string rid;
  std::uint64_t header_bytes_sent;
  std::uint64_t retransmitted_packets_sent;
  std::uint64_t retransmitted_bytes_sent;
  std::uint32_t rtx_ssrc;
  double target_bitrate;
  std::uint64_t total_encoded_bytes_target;
  std::uint32_t frame_width;
  std::uint32_t frame_height;
  double frames_per_second;
  std::uint32_t frames_sent;
  std::uint32_t huge_frames_sent;
  std::uint32_t frames_encoded;
  std::uint32_t key_frames_encoded;
  std::uint64_t qp_sum;
  double total_encode_time;
  double total_packet_send_delay;
  QualityLimitationReason quality_limitation_reason;
  std::unordered_map<std::string, double> quality_limitation_durations;
  std::uint32_t quality_limitation_resolution_changes;
  std::uint32_t nack_count;
  std::uint32_t fir_count;
  std::uint32_t pli_count;
  std::string encoder_implementation;
  bool power_efficient_encoder;
  bool active;
  std::string scalability_mode;
};

struct RemoteInboundRtpStreamStats {
  std::string local_id;
  double round_trip_time;
  double total_round_trip_time;
  double fraction_lost;
  std::uint64_t round_trip_time_measurements;
};

struct RemoteOutboundRtpStreamStats {
  std::string local_id;
  double remote_timestamp;
  std::uint64_t reports_sent;
  double round_trip_time;
  double total_round_trip_time;
  std::uint64_t round_trip_time_measurements;
};

struct MediaSourceStats {
  std::string track_identifier;
  std::string kind;
};

struct AudioSourceStats {
  double audio_level;
  double total_audio_energy;
  double total_samples_duration;
  double echo_return_loss;
  double echo_return_loss_enhancement;
  double dropped_samples_duration;
  std::uint32_t dropped_samples_events;
  double total_capture_delay;
  std::uint64_t total_samples_captured;
};

struct VideoSourceStats {
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t frames;
  double frames_per_second;
};

struct AudioPlayoutStats {
  std::string kind;
  double synthesized_samples_duration;
  std::uint32_t synthesized_samples_events;
  double total_samples_duration;
  double total_playout_delay;
  std::uint64_t total_samples_count;
};

struct PeerConnectionStats {
  std::uint32_t data_channels_opened;
  std::uint32_t data_channels_closed;
};

struct DataChannelStats {
  std::string label;
  std::string protocol;
  std::int32_t data_channel_identifier;
  std::optional<DataChannelState> state;
  std::uint32_t messages_sent;
  std::uint64_t bytes_sent;
  std::uint32_t messages_received;
  std::uint64_t bytes_received;
};

struct TransportStats {
  std::uint64_t packets_sent;
  std::uint64_t packets_received;
  std::uint64_t bytes_sent;
  std::uint64_t bytes_received;
  IceRole ice_role;
  std::string ice_local_username_fragment;
  std::optional<DtlsTransportState> dtls_state;
  std::optional<IceTransportState> ice_state;
  std::string selected_candidate_pair_id;
  std::string local_certificate_id;
  std::string remote_certificate_id;
  std::string tls_version;
  std::string dtls_cipher;
  DtlsRole dtls_role;
  std::string srtp_cipher;
  std::uint32_t selected_candidate_pair_changes;
};

struct CandidatePairStats {
  std::string transport_id;
  std::string local_candidate_id;
  std::string remote_candidate_id;
  std::optional<IceCandidatePairState> state;
  bool nominated;
  std::uint64_t packets_sent;
  std::uint64_t packets_received;
  std::uint64_t bytes_sent;
  std::uint64_t bytes_received;
  double last_packet_sent_timestamp;
  double last_packet_received_timestamp;
  double total_round_trip_time;
  double current_round_trip_time;
  double available_outgoing_bitrate;
  double available_incoming_bitrate;
  std::uint64_t requests_received;
  std::uint64_t requests_sent;
  std::uint64_t responses_received;
  std::uint64_t responses_sent;
  std::uint64_t consent_requests_sent;
  std::uint32_t packets_discarded_on_send;
  std::uint64_t bytes_discarded_on_send;
};

struct IceCandidateStats {
  std::string transport_id;
  std::string address;
  std::int32_t port;
  std::string protocol;
  std::optional<IceCandidateType> candidate_type;
  std::int32_t priority;
  std::string url;
  std::optional<IceServerTransportProtocol> relay_protocol;
  std::string foundation;
  std::string related_address;
  std::int32_t related_port;
  std::string username_fragment;
  std::optional<IceTcpCandidateType> tcp_type;
};

struct CertificateStats {
  std::string fingerprint;
  std::string fingerprint_algorithm;
  std::string base64_certificate;
  std::string issuer_certificate_id;
};

struct StreamStats {
  std::string id;
  std::string stream_identifier;
};

// ----------------------
// High-level RtcStats wrapper
// ----------------------

struct RtcCodecStats {
  RtcStatsData rtc;
  CodecStats codec;
};

struct RtcInboundRtpStats {
  RtcStatsData rtc;
  RtpStreamStats stream;
  ReceivedRtpStreamStats received;
  InboundRtpStreamStats inbound;
};

struct RtcOutboundRtpStats {
  RtcStatsData rtc;
  RtpStreamStats stream;
  SentRtpStreamStats sent;
  OutboundRtpStreamStats outbound;
};

struct RtcRemoteInboundRtpStats {
  RtcStatsData rtc;
  RtpStreamStats stream;
  ReceivedRtpStreamStats received;
  RemoteInboundRtpStreamStats remote_inbound;
};

struct RtcRemoteOutboundRtpStats {
  RtcStatsData rtc;
  RtpStreamStats stream;
  SentRtpStreamStats sent;
  RemoteOutboundRtpStreamStats remote_outbound;
};

struct RtcMediaSourceStats {
  RtcStatsData rtc;
  MediaSourceStats source;
  AudioSourceStats audio;
  VideoSourceStats video;
};

struct RtcMediaPlayoutStats {
  RtcStatsData rtc;
  AudioPlayoutStats audio_playout;
};

struct RtcPeerConnectionStats {
  RtcStatsData rtc;
  PeerConnectionStats pc;
};

struct RtcDataChannelStats {
  RtcStatsData rtc;
  DataChannelStats dc;
};

struct RtcTransportStats {
  RtcStatsData rtc;
  TransportStats transport;
};

struct RtcCandidatePairStats {
  RtcStatsData rtc;
  CandidatePairStats candidate_pair;
};

struct RtcLocalCandidateStats {
  RtcStatsData rtc;
  IceCandidateStats candidate;
};

struct RtcRemoteCandidateStats {
  RtcStatsData rtc;
  IceCandidateStats candidate;
};

struct RtcCertificateStats {
  RtcStatsData rtc;
  CertificateStats certificate;
};

struct RtcStreamStats {
  RtcStatsData rtc;
  StreamStats stream;
};

// Deprecated Track omitted on purpose.

using RtcStatsVariant =
    std::variant<RtcCodecStats, RtcInboundRtpStats, RtcOutboundRtpStats,
                 RtcRemoteInboundRtpStats, RtcRemoteOutboundRtpStats,
                 RtcMediaSourceStats, RtcMediaPlayoutStats,
                 RtcPeerConnectionStats, RtcDataChannelStats, RtcTransportStats,
                 RtcCandidatePairStats, RtcLocalCandidateStats,
                 RtcRemoteCandidateStats, RtcCertificateStats, RtcStreamStats>;

struct RtcStats {
  RtcStatsVariant stats;
};

// ----------------------
// fromProto declarations
// ----------------------

RtcStatsData fromProto(const proto::RtcStatsData &);

CodecStats fromProto(const proto::CodecStats &);
RtpStreamStats fromProto(const proto::RtpStreamStats &);
ReceivedRtpStreamStats fromProto(const proto::ReceivedRtpStreamStats &);
InboundRtpStreamStats fromProto(const proto::InboundRtpStreamStats &);
SentRtpStreamStats fromProto(const proto::SentRtpStreamStats &);
OutboundRtpStreamStats fromProto(const proto::OutboundRtpStreamStats &);
RemoteInboundRtpStreamStats
fromProto(const proto::RemoteInboundRtpStreamStats &);
RemoteOutboundRtpStreamStats
fromProto(const proto::RemoteOutboundRtpStreamStats &);
MediaSourceStats fromProto(const proto::MediaSourceStats &);
AudioSourceStats fromProto(const proto::AudioSourceStats &);
VideoSourceStats fromProto(const proto::VideoSourceStats &);
AudioPlayoutStats fromProto(const proto::AudioPlayoutStats &);
PeerConnectionStats fromProto(const proto::PeerConnectionStats &);
DataChannelStats fromProto(const proto::DataChannelStats &);
TransportStats fromProto(const proto::TransportStats &);
CandidatePairStats fromProto(const proto::CandidatePairStats &);
IceCandidateStats fromProto(const proto::IceCandidateStats &);
CertificateStats fromProto(const proto::CertificateStats &);
StreamStats fromProto(const proto::StreamStats &);

// High-level:
RtcStats fromProto(const proto::RtcStats &);

// helper if you have repeated RtcStats in proto:
std::vector<RtcStats> fromProto(const std::vector<proto::RtcStats> &);

} // namespace livekit
