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

#include "livekit/visibility.h"

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

/// State of a WebRTC data channel.
enum class DataChannelState {
  Connecting,
  Open,
  Closing,
  Closed,
  Unknown,
};

/// Reason outbound media quality is currently limited.
enum class QualityLimitationReason {
  None,
  Cpu,
  Bandwidth,
  Other,
};

/// ICE role used by a transport.
enum class IceRole {
  Unknown,
  Controlling,
  Controlled,
};

/// DTLS transport state.
enum class DtlsTransportState {
  New,
  Connecting,
  Connected,
  Closed,
  Failed,
  Unknown,
};

/// ICE transport state.
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

/// DTLS role used by a transport.
enum class DtlsRole {
  Client,
  Server,
  Unknown,
};

/// State of an ICE candidate pair.
enum class IceCandidatePairState {
  Frozen,
  Waiting,
  InProgress,
  Failed,
  Succeeded,
  Unknown,
};

/// Type of ICE candidate.
enum class IceCandidateType {
  Host,
  Srflx,
  Prflx,
  Relay,
  Unknown,
};

/// Transport protocol used by an ICE server.
enum class IceServerTransportProtocol {
  Udp,
  Tcp,
  Tls,
  Unknown,
};

/// TCP candidate type for an ICE candidate.
enum class IceTcpCandidateType {
  Active,
  Passive,
  So,
  Unknown,
};

// ----------------------
// Leaf stats types
// ----------------------

/// Base data shared by RTC stats records.
struct RtcStatsData {
  std::string id;
  std::int64_t timestamp_ms;
};

/// Codec statistics for an RTP stream.
struct CodecStats {
  std::uint32_t payload_type;
  std::string transport_id;
  std::string mime_type;
  std::uint32_t clock_rate;
  std::uint32_t channels;
  std::string sdp_fmtp_line;
};

/// Base statistics for an RTP stream.
struct RtpStreamStats {
  std::uint32_t ssrc;
  std::string kind;
  std::string transport_id;
  std::string codec_id;
};

/// Statistics for received RTP streams.
struct ReceivedRtpStreamStats {
  std::uint64_t packets_received;
  std::int64_t packets_lost;
  double jitter;
};

/// Statistics for inbound RTP media.
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

/// Statistics for sent RTP streams.
struct SentRtpStreamStats {
  std::uint64_t packets_sent;
  std::uint64_t bytes_sent;
};

/// Statistics for outbound RTP media.
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

/// Statistics reported by a remote receiver for a local outbound RTP stream.
struct RemoteInboundRtpStreamStats {
  std::string local_id;
  double round_trip_time;
  double total_round_trip_time;
  double fraction_lost;
  std::uint64_t round_trip_time_measurements;
};

/// Statistics reported by a remote sender for a local inbound RTP stream.
struct RemoteOutboundRtpStreamStats {
  std::string local_id;
  double remote_timestamp;
  std::uint64_t reports_sent;
  double round_trip_time;
  double total_round_trip_time;
  std::uint64_t round_trip_time_measurements;
};

/// Common statistics for a local media source.
struct MediaSourceStats {
  std::string track_identifier;
  std::string kind;
};

/// Statistics for a local audio source.
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

/// Statistics for a local video source.
struct VideoSourceStats {
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t frames;
  double frames_per_second;
};

/// @brief Statistics for audio playout performance.
///
/// Contains metrics about audio sample synthesis and playout timing,
/// useful for monitoring audio quality and detecting issues like underruns.
struct AudioPlayoutStats {
  std::string kind;                         ///< The type of media ("audio").
  double synthesized_samples_duration;      ///< Duration of synthesized samples in
                                            ///< seconds.
  std::uint32_t synthesized_samples_events; ///< Number of synthesis events
                                            ///< (e.g., concealment).
  double total_samples_duration;            ///< Total duration of all samples in seconds.
  double total_playout_delay;               ///< Cumulative playout delay in seconds.
  std::uint64_t total_samples_count;        ///< Total number of samples played out.
};

/// Statistics for a peer connection.
struct PeerConnectionStats {
  std::uint32_t data_channels_opened;
  std::uint32_t data_channels_closed;
};

/// Statistics for a WebRTC data channel.
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

/// Statistics for a WebRTC transport.
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

/// Statistics for a selected or candidate ICE pair.
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

/// Statistics for a local or remote ICE candidate.
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

/// Statistics for a DTLS certificate.
struct CertificateStats {
  std::string fingerprint;
  std::string fingerprint_algorithm;
  std::string base64_certificate;
  std::string issuer_certificate_id;
};

/// Statistics for a media stream.
struct StreamStats {
  std::string id;
  std::string stream_identifier;
};

// ----------------------
// High-level RtcStats wrapper
// ----------------------

/// Typed RTC stats wrapper for codec statistics.
struct RtcCodecStats {
  RtcStatsData rtc;
  CodecStats codec;
};

/// Typed RTC stats wrapper for inbound RTP statistics.
struct RtcInboundRtpStats {
  RtcStatsData rtc;
  RtpStreamStats stream;
  ReceivedRtpStreamStats received;
  InboundRtpStreamStats inbound;
};

/// Typed RTC stats wrapper for outbound RTP statistics.
struct RtcOutboundRtpStats {
  RtcStatsData rtc;
  RtpStreamStats stream;
  SentRtpStreamStats sent;
  OutboundRtpStreamStats outbound;
};

/// Typed RTC stats wrapper for remote inbound RTP statistics.
struct RtcRemoteInboundRtpStats {
  RtcStatsData rtc;
  RtpStreamStats stream;
  ReceivedRtpStreamStats received;
  RemoteInboundRtpStreamStats remote_inbound;
};

/// Typed RTC stats wrapper for remote outbound RTP statistics.
struct RtcRemoteOutboundRtpStats {
  RtcStatsData rtc;
  RtpStreamStats stream;
  SentRtpStreamStats sent;
  RemoteOutboundRtpStreamStats remote_outbound;
};

/// Typed RTC stats wrapper for media source statistics.
struct RtcMediaSourceStats {
  RtcStatsData rtc;
  MediaSourceStats source;
  AudioSourceStats audio;
  VideoSourceStats video;
};

/// Typed RTC stats wrapper for audio playout statistics.
struct RtcMediaPlayoutStats {
  RtcStatsData rtc;
  AudioPlayoutStats audio_playout;
};

/// Typed RTC stats wrapper for peer connection statistics.
struct RtcPeerConnectionStats {
  RtcStatsData rtc;
  PeerConnectionStats pc;
};

/// Typed RTC stats wrapper for data channel statistics.
struct RtcDataChannelStats {
  RtcStatsData rtc;
  DataChannelStats dc;
};

/// Typed RTC stats wrapper for transport statistics.
struct RtcTransportStats {
  RtcStatsData rtc;
  TransportStats transport;
};

/// Typed RTC stats wrapper for ICE candidate pair statistics.
struct RtcCandidatePairStats {
  RtcStatsData rtc;
  CandidatePairStats candidate_pair;
};

/// Typed RTC stats wrapper for local ICE candidate statistics.
struct RtcLocalCandidateStats {
  RtcStatsData rtc;
  IceCandidateStats candidate;
};

/// Typed RTC stats wrapper for remote ICE candidate statistics.
struct RtcRemoteCandidateStats {
  RtcStatsData rtc;
  IceCandidateStats candidate;
};

/// Typed RTC stats wrapper for certificate statistics.
struct RtcCertificateStats {
  RtcStatsData rtc;
  CertificateStats certificate;
};

/// Typed RTC stats wrapper for media stream statistics.
struct RtcStreamStats {
  RtcStatsData rtc;
  StreamStats stream;
};

// Deprecated Track omitted on purpose.

using RtcStatsVariant =
    std::variant<RtcCodecStats, RtcInboundRtpStats, RtcOutboundRtpStats, RtcRemoteInboundRtpStats,
                 RtcRemoteOutboundRtpStats, RtcMediaSourceStats, RtcMediaPlayoutStats, RtcPeerConnectionStats,
                 RtcDataChannelStats, RtcTransportStats, RtcCandidatePairStats, RtcLocalCandidateStats,
                 RtcRemoteCandidateStats, RtcCertificateStats, RtcStreamStats>;

/// Variant wrapper for typed RTC stats records.
struct RtcStats {
  RtcStatsVariant stats;
};

// ----------------------
// fromProto declarations
// ----------------------

LIVEKIT_API RtcStatsData fromProto(const proto::RtcStatsData&);

LIVEKIT_API CodecStats fromProto(const proto::CodecStats&);
LIVEKIT_API RtpStreamStats fromProto(const proto::RtpStreamStats&);
LIVEKIT_API ReceivedRtpStreamStats fromProto(const proto::ReceivedRtpStreamStats&);
LIVEKIT_API InboundRtpStreamStats fromProto(const proto::InboundRtpStreamStats&);
LIVEKIT_API SentRtpStreamStats fromProto(const proto::SentRtpStreamStats&);
LIVEKIT_API OutboundRtpStreamStats fromProto(const proto::OutboundRtpStreamStats&);
LIVEKIT_API RemoteInboundRtpStreamStats fromProto(const proto::RemoteInboundRtpStreamStats&);
LIVEKIT_API RemoteOutboundRtpStreamStats fromProto(const proto::RemoteOutboundRtpStreamStats&);
LIVEKIT_API MediaSourceStats fromProto(const proto::MediaSourceStats&);
LIVEKIT_API AudioSourceStats fromProto(const proto::AudioSourceStats&);
LIVEKIT_API VideoSourceStats fromProto(const proto::VideoSourceStats&);
LIVEKIT_API AudioPlayoutStats fromProto(const proto::AudioPlayoutStats&);
LIVEKIT_API PeerConnectionStats fromProto(const proto::PeerConnectionStats&);
LIVEKIT_API DataChannelStats fromProto(const proto::DataChannelStats&);
LIVEKIT_API TransportStats fromProto(const proto::TransportStats&);
LIVEKIT_API CandidatePairStats fromProto(const proto::CandidatePairStats&);
LIVEKIT_API IceCandidateStats fromProto(const proto::IceCandidateStats&);
LIVEKIT_API CertificateStats fromProto(const proto::CertificateStats&);
LIVEKIT_API StreamStats fromProto(const proto::StreamStats&);

// High-level:
LIVEKIT_API RtcStats fromProto(const proto::RtcStats&);

// helper if you have repeated RtcStats in proto:
LIVEKIT_API std::vector<RtcStats> fromProto(const std::vector<proto::RtcStats>&);

} // namespace livekit
