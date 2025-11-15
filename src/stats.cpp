#include "livekit/stats.h"

#include "stats.pb.h"

namespace livekit {

namespace {

// --- enum helpers ---

DataChannelState fromProto(livekit::proto::DataChannelState s) {
  using P = livekit::proto::DataChannelState;
  switch (s) {
    case P::DC_CONNECTING: return DataChannelState::Connecting;
    case P::DC_OPEN:       return DataChannelState::Open;
    case P::DC_CLOSING:    return DataChannelState::Closing;
    case P::DC_CLOSED:     return DataChannelState::Closed;
    default:               return DataChannelState::Unknown;
  }
}

QualityLimitationReason fromProto(livekit::proto::QualityLimitationReason r) {
  using P = livekit::proto::QualityLimitationReason;
  switch (r) {
    case P::LIMITATION_NONE:      return QualityLimitationReason::None;
    case P::LIMITATION_CPU:       return QualityLimitationReason::Cpu;
    case P::LIMITATION_BANDWIDTH: return QualityLimitationReason::Bandwidth;
    case P::LIMITATION_OTHER:     return QualityLimitationReason::Other;
    default:                      return QualityLimitationReason::Other;
  }
}

IceRole fromProto(livekit::proto::IceRole r) {
  using P = livekit::proto::IceRole;
  switch (r) {
    case P::ICE_CONTROLLING: return IceRole::Controlling;
    case P::ICE_CONTROLLED:  return IceRole::Controlled;
    case P::ICE_UNKNOWN:
    default:                 return IceRole::Unknown;
  }
}

DtlsTransportState fromProto(livekit::proto::DtlsTransportState s) {
  using P = livekit::proto::DtlsTransportState;
  switch (s) {
    case P::DTLS_TRANSPORT_NEW:        return DtlsTransportState::New;
    case P::DTLS_TRANSPORT_CONNECTING: return DtlsTransportState::Connecting;
    case P::DTLS_TRANSPORT_CONNECTED:  return DtlsTransportState::Connected;
    case P::DTLS_TRANSPORT_CLOSED:     return DtlsTransportState::Closed;
    case P::DTLS_TRANSPORT_FAILED:     return DtlsTransportState::Failed;
    default:                           return DtlsTransportState::Unknown;
  }
}

IceTransportState fromProto(livekit::proto::IceTransportState s) {
  using P = livekit::proto::IceTransportState;
  switch (s) {
    case P::ICE_TRANSPORT_NEW:          return IceTransportState::New;
    case P::ICE_TRANSPORT_CHECKING:     return IceTransportState::Checking;
    case P::ICE_TRANSPORT_CONNECTED:    return IceTransportState::Connected;
    case P::ICE_TRANSPORT_COMPLETED:    return IceTransportState::Completed;
    case P::ICE_TRANSPORT_DISCONNECTED: return IceTransportState::Disconnected;
    case P::ICE_TRANSPORT_FAILED:       return IceTransportState::Failed;
    case P::ICE_TRANSPORT_CLOSED:       return IceTransportState::Closed;
    default:                            return IceTransportState::Unknown;
  }
}

DtlsRole fromProto(livekit::proto::DtlsRole r) {
  using P = livekit::proto::DtlsRole;
  switch (r) {
    case P::DTLS_CLIENT:  return DtlsRole::Client;
    case P::DTLS_SERVER:  return DtlsRole::Server;
    case P::DTLS_UNKNOWN:
    default:              return DtlsRole::Unknown;
  }
}

IceCandidatePairState fromProto(livekit::proto::IceCandidatePairState s) {
  using P = livekit::proto::IceCandidatePairState;
  switch (s) {
    case P::PAIR_FROZEN:     return IceCandidatePairState::Frozen;
    case P::PAIR_WAITING:    return IceCandidatePairState::Waiting;
    case P::PAIR_IN_PROGRESS:return IceCandidatePairState::InProgress;
    case P::PAIR_FAILED:     return IceCandidatePairState::Failed;
    case P::PAIR_SUCCEEDED:  return IceCandidatePairState::Succeeded;
    default:                 return IceCandidatePairState::Unknown;
  }
}

IceCandidateType fromProto(livekit::proto::IceCandidateType t) {
  using P = livekit::proto::IceCandidateType;
  switch (t) {
    case P::HOST:  return IceCandidateType::Host;
    case P::SRFLX: return IceCandidateType::Srflx;
    case P::PRFLX: return IceCandidateType::Prflx;
    case P::RELAY: return IceCandidateType::Relay;
    default:       return IceCandidateType::Unknown;
  }
}

IceServerTransportProtocol fromProto(livekit::proto::IceServerTransportProtocol p) {
  using P = livekit::proto::IceServerTransportProtocol;
  switch (p) {
    case P::TRANSPORT_UDP: return IceServerTransportProtocol::Udp;
    case P::TRANSPORT_TCP: return IceServerTransportProtocol::Tcp;
    case P::TRANSPORT_TLS: return IceServerTransportProtocol::Tls;
    default:               return IceServerTransportProtocol::Unknown;
  }
}

IceTcpCandidateType fromProto(livekit::proto::IceTcpCandidateType t) {
  using P = livekit::proto::IceTcpCandidateType;
  switch (t) {
    case P::CANDIDATE_ACTIVE:  return IceTcpCandidateType::Active;
    case P::CANDIDATE_PASSIVE: return IceTcpCandidateType::Passive;
    case P::CANDIDATE_SO:      return IceTcpCandidateType::So;
    default:                   return IceTcpCandidateType::Unknown;
  }
}

}  // namespace

// ----------------------
// Leaf conversions
// ----------------------

RtcStatsData fromProto(const proto::RtcStatsData& s) {
  RtcStatsData out;
  out.id = s.id();
  out.timestamp_ms = s.timestamp();
  return out;
}

CodecStats fromProto(const proto::CodecStats& s) {
  CodecStats out;
  out.payload_type = s.payload_type();
  out.transport_id = s.transport_id();
  out.mime_type = s.mime_type();
  out.clock_rate = s.clock_rate();
  out.channels = s.channels();
  out.sdp_fmtp_line = s.sdp_fmtp_line();
  return out;
}

RtpStreamStats fromProto(const proto::RtpStreamStats& s) {
  RtpStreamStats out;
  out.ssrc = s.ssrc();
  out.kind = s.kind();
  out.transport_id = s.transport_id();
  out.codec_id = s.codec_id();
  return out;
}

ReceivedRtpStreamStats fromProto(const proto::ReceivedRtpStreamStats& s) {
  ReceivedRtpStreamStats out;
  out.packets_received = s.packets_received();
  out.packets_lost = s.packets_lost();
  out.jitter = s.jitter();
  return out;
}

InboundRtpStreamStats fromProto(const proto::InboundRtpStreamStats& s) {
  InboundRtpStreamStats out;
  out.track_identifier = s.track_identifier();
  out.mid = s.mid();
  out.remote_id = s.remote_id();
  out.frames_decoded = s.frames_decoded();
  out.key_frames_decoded = s.key_frames_decoded();
  out.frames_rendered = s.frames_rendered();
  out.frames_dropped = s.frames_dropped();
  out.frame_width = s.frame_width();
  out.frame_height = s.frame_height();
  out.frames_per_second = s.frames_per_second();
  out.qp_sum = s.qp_sum();
  out.total_decode_time = s.total_decode_time();
  out.total_inter_frame_delay = s.total_inter_frame_delay();
  out.total_squared_inter_frame_delay = s.total_squared_inter_frame_delay();
  out.pause_count = s.pause_count();
  out.total_pause_duration = s.total_pause_duration();
  out.freeze_count = s.freeze_count();
  out.total_freeze_duration = s.total_freeze_duration();
  out.last_packet_received_timestamp = s.last_packet_received_timestamp();
  out.header_bytes_received = s.header_bytes_received();
  out.packets_discarded = s.packets_discarded();
  out.fec_bytes_received = s.fec_bytes_received();
  out.fec_packets_received = s.fec_packets_received();
  out.fec_packets_discarded = s.fec_packets_discarded();
  out.bytes_received = s.bytes_received();
  out.nack_count = s.nack_count();
  out.fir_count = s.fir_count();
  out.pli_count = s.pli_count();
  out.total_processing_delay = s.total_processing_delay();
  out.estimated_playout_timestamp = s.estimated_playout_timestamp();
  out.jitter_buffer_delay = s.jitter_buffer_delay();
  out.jitter_buffer_target_delay = s.jitter_buffer_target_delay();
  out.jitter_buffer_emitted_count = s.jitter_buffer_emitted_count();
  out.jitter_buffer_minimum_delay = s.jitter_buffer_minimum_delay();
  out.total_samples_received = s.total_samples_received();
  out.concealed_samples = s.concealed_samples();
  out.silent_concealed_samples = s.silent_concealed_samples();
  out.concealment_events = s.concealment_events();
  out.inserted_samples_for_deceleration = s.inserted_samples_for_deceleration();
  out.removed_samples_for_acceleration = s.removed_samples_for_acceleration();
  out.audio_level = s.audio_level();
  out.total_audio_energy = s.total_audio_energy();
  out.total_samples_duration = s.total_samples_duration();
  out.frames_received = s.frames_received();
  out.decoder_implementation = s.decoder_implementation();
  out.playout_id = s.playout_id();
  out.power_efficient_decoder = s.power_efficient_decoder();
  out.frames_assembled_from_multiple_packets = s.frames_assembled_from_multiple_packets();
  out.total_assembly_time = s.total_assembly_time();
  out.retransmitted_packets_received = s.retransmitted_packets_received();
  out.retransmitted_bytes_received = s.retransmitted_bytes_received();
  out.rtx_ssrc = s.rtx_ssrc();
  out.fec_ssrc = s.fec_ssrc();
  return out;
}

SentRtpStreamStats fromProto(const proto::SentRtpStreamStats& s) {
  SentRtpStreamStats out;
  out.packets_sent = s.packets_sent();
  out.bytes_sent = s.bytes_sent();
  return out;
}

OutboundRtpStreamStats fromProto(const proto::OutboundRtpStreamStats& s) {
  OutboundRtpStreamStats out;
  out.mid = s.mid();
  out.media_source_id = s.media_source_id();
  out.remote_id = s.remote_id();
  out.rid = s.rid();
  out.header_bytes_sent = s.header_bytes_sent();
  out.retransmitted_packets_sent = s.retransmitted_packets_sent();
  out.retransmitted_bytes_sent = s.retransmitted_bytes_sent();
  out.rtx_ssrc = s.rtx_ssrc();
  out.target_bitrate = s.target_bitrate();
  out.total_encoded_bytes_target = s.total_encoded_bytes_target();
  out.frame_width = s.frame_width();
  out.frame_height = s.frame_height();
  out.frames_per_second = s.frames_per_second();
  out.frames_sent = s.frames_sent();
  out.huge_frames_sent = s.huge_frames_sent();
  out.frames_encoded = s.frames_encoded();
  out.key_frames_encoded = s.key_frames_encoded();
  out.qp_sum = s.qp_sum();
  out.total_encode_time = s.total_encode_time();
  out.total_packet_send_delay = s.total_packet_send_delay();
  out.quality_limitation_reason = fromProto(s.quality_limitation_reason());
  out.quality_limitation_durations.clear();
  for (const auto& kv : s.quality_limitation_durations()) {
    out.quality_limitation_durations.emplace(kv.first, kv.second);
  }
  out.quality_limitation_resolution_changes = s.quality_limitation_resolution_changes();
  out.nack_count = s.nack_count();
  out.fir_count = s.fir_count();
  out.pli_count = s.pli_count();
  out.encoder_implementation = s.encoder_implementation();
  out.power_efficient_encoder = s.power_efficient_encoder();
  out.active = s.active();
  out.scalability_mode = s.scalability_mode();
  return out;
}

RemoteInboundRtpStreamStats fromProto(const proto::RemoteInboundRtpStreamStats& s) {
  RemoteInboundRtpStreamStats out;
  out.local_id = s.local_id();
  out.round_trip_time = s.round_trip_time();
  out.total_round_trip_time = s.total_round_trip_time();
  out.fraction_lost = s.fraction_lost();
  out.round_trip_time_measurements = s.round_trip_time_measurements();
  return out;
}

RemoteOutboundRtpStreamStats fromProto(const proto::RemoteOutboundRtpStreamStats& s) {
  RemoteOutboundRtpStreamStats out;
  out.local_id = s.local_id();
  out.remote_timestamp = s.remote_timestamp();
  out.reports_sent = s.reports_sent();
  out.round_trip_time = s.round_trip_time();
  out.total_round_trip_time = s.total_round_trip_time();
  out.round_trip_time_measurements = s.round_trip_time_measurements();
  return out;
}

MediaSourceStats fromProto(const proto::MediaSourceStats& s) {
  MediaSourceStats out;
  out.track_identifier = s.track_identifier();
  out.kind = s.kind();
  return out;
}

AudioSourceStats fromProto(const proto::AudioSourceStats& s) {
  AudioSourceStats out;
  out.audio_level = s.audio_level();
  out.total_audio_energy = s.total_audio_energy();
  out.total_samples_duration = s.total_samples_duration();
  out.echo_return_loss = s.echo_return_loss();
  out.echo_return_loss_enhancement = s.echo_return_loss_enhancement();
  out.dropped_samples_duration = s.dropped_samples_duration();
  out.dropped_samples_events = s.dropped_samples_events();
  out.total_capture_delay = s.total_capture_delay();
  out.total_samples_captured = s.total_samples_captured();
  return out;
}

VideoSourceStats fromProto(const proto::VideoSourceStats& s) {
  VideoSourceStats out;
  out.width = s.width();
  out.height = s.height();
  out.frames = s.frames();
  out.frames_per_second = s.frames_per_second();
  return out;
}

AudioPlayoutStats fromProto(const proto::AudioPlayoutStats& s) {
  AudioPlayoutStats out;
  out.kind = s.kind();
  out.synthesized_samples_duration = s.synthesized_samples_duration();
  out.synthesized_samples_events = s.synthesized_samples_events();
  out.total_samples_duration = s.total_samples_duration();
  out.total_playout_delay = s.total_playout_delay();
  out.total_samples_count = s.total_samples_count();
  return out;
}

PeerConnectionStats fromProto(const proto::PeerConnectionStats& s) {
  PeerConnectionStats out;
  out.data_channels_opened = s.data_channels_opened();
  out.data_channels_closed = s.data_channels_closed();
  return out;
}

DataChannelStats fromProto(const proto::DataChannelStats& s) {
  DataChannelStats out;
  out.label = s.label();
  out.protocol = s.protocol();
  out.data_channel_identifier = s.data_channel_identifier();
  if (s.has_state()) {
    out.state = fromProto(s.state());
  } else {
    out.state.reset();
  }
  out.messages_sent = s.messages_sent();
  out.bytes_sent = s.bytes_sent();
  out.messages_received = s.messages_received();
  out.bytes_received = s.bytes_received();
  return out;
}

TransportStats fromProto(const proto::TransportStats& s) {
  TransportStats out;
  out.packets_sent = s.packets_sent();
  out.packets_received = s.packets_received();
  out.bytes_sent = s.bytes_sent();
  out.bytes_received = s.bytes_received();
  out.ice_role = fromProto(s.ice_role());
  out.ice_local_username_fragment = s.ice_local_username_fragment();
  if (s.has_dtls_state()) {
    out.dtls_state = fromProto(s.dtls_state());
  } else {
    out.dtls_state.reset();
  }
  if (s.has_ice_state()) {
    out.ice_state = fromProto(s.ice_state());
  } else {
    out.ice_state.reset();
  }
  out.selected_candidate_pair_id = s.selected_candidate_pair_id();
  out.local_certificate_id = s.local_certificate_id();
  out.remote_certificate_id = s.remote_certificate_id();
  out.tls_version = s.tls_version();
  out.dtls_cipher = s.dtls_cipher();
  out.dtls_role = fromProto(s.dtls_role());
  out.srtp_cipher = s.srtp_cipher();
  out.selected_candidate_pair_changes = s.selected_candidate_pair_changes();
  return out;
}

CandidatePairStats fromProto(const proto::CandidatePairStats& s) {
  CandidatePairStats out;
  out.transport_id = s.transport_id();
  out.local_candidate_id = s.local_candidate_id();
  out.remote_candidate_id = s.remote_candidate_id();
  if (s.has_state()) {
    out.state = fromProto(s.state());
  } else {
    out.state.reset();
  }
  out.nominated = s.nominated();
  out.packets_sent = s.packets_sent();
  out.packets_received = s.packets_received();
  out.bytes_sent = s.bytes_sent();
  out.bytes_received = s.bytes_received();
  out.last_packet_sent_timestamp = s.last_packet_sent_timestamp();
  out.last_packet_received_timestamp = s.last_packet_received_timestamp();
  out.total_round_trip_time = s.total_round_trip_time();
  out.current_round_trip_time = s.current_round_trip_time();
  out.available_outgoing_bitrate = s.available_outgoing_bitrate();
  out.available_incoming_bitrate = s.available_incoming_bitrate();
  out.requests_received = s.requests_received();
  out.requests_sent = s.requests_sent();
  out.responses_received = s.responses_received();
  out.responses_sent = s.responses_sent();
  out.consent_requests_sent = s.consent_requests_sent();
  out.packets_discarded_on_send = s.packets_discarded_on_send();
  out.bytes_discarded_on_send = s.bytes_discarded_on_send();
  return out;
}

IceCandidateStats fromProto(const proto::IceCandidateStats& s) {
  IceCandidateStats out;
  out.transport_id = s.transport_id();
  out.address = s.address();
  out.port = s.port();
  out.protocol = s.protocol();
  if (s.has_candidate_type()) {
    out.candidate_type = fromProto(s.candidate_type());
  } else {
    out.candidate_type.reset();
  }
  out.priority = s.priority();
  out.url = s.url();
  if (s.has_relay_protocol()) {
    out.relay_protocol = fromProto(s.relay_protocol());
  } else {
    out.relay_protocol.reset();
  }
  out.foundation = s.foundation();
  out.related_address = s.related_address();
  out.related_port = s.related_port();
  out.username_fragment = s.username_fragment();
  if (s.has_tcp_type()) {
    out.tcp_type = fromProto(s.tcp_type());
  } else {
    out.tcp_type.reset();
  }
  return out;
}

CertificateStats fromProto(const proto::CertificateStats& s) {
  CertificateStats out;
  out.fingerprint = s.fingerprint();
  out.fingerprint_algorithm = s.fingerprint_algorithm();
  out.base64_certificate = s.base64_certificate();
  out.issuer_certificate_id = s.issuer_certificate_id();
  return out;
}

StreamStats fromProto(const proto::StreamStats& s) {
  StreamStats out;
  out.id = s.id();
  out.stream_identifier = s.stream_identifier();
  return out;
}

// ----------------------
// High-level RtcStats fromProto
// ----------------------

RtcStats fromProto(const proto::RtcStats& s) {
  using P = proto::RtcStats;

  switch (s.stats_case()) {
    case P::kCodec: {
      RtcCodecStats out;
      out.rtc = fromProto(s.codec().rtc());
      out.codec = fromProto(s.codec().codec());
      return RtcStats{std::move(out)};
    }
    case P::kInboundRtp: {
      RtcInboundRtpStats out;
      out.rtc = fromProto(s.inbound_rtp().rtc());
      out.stream = fromProto(s.inbound_rtp().stream());
      out.received = fromProto(s.inbound_rtp().received());
      out.inbound = fromProto(s.inbound_rtp().inbound());
      return RtcStats{std::move(out)};
    }
    case P::kOutboundRtp: {
      RtcOutboundRtpStats out;
      out.rtc = fromProto(s.outbound_rtp().rtc());
      out.stream = fromProto(s.outbound_rtp().stream());
      out.sent = fromProto(s.outbound_rtp().sent());
      out.outbound = fromProto(s.outbound_rtp().outbound());
      return RtcStats{std::move(out)};
    }
    case P::kRemoteInboundRtp: {
      RtcRemoteInboundRtpStats out;
      out.rtc = fromProto(s.remote_inbound_rtp().rtc());
      out.stream = fromProto(s.remote_inbound_rtp().stream());
      out.received = fromProto(s.remote_inbound_rtp().received());
      out.remote_inbound = fromProto(s.remote_inbound_rtp().remote_inbound());
      return RtcStats{std::move(out)};
    }
    case P::kRemoteOutboundRtp: {
      RtcRemoteOutboundRtpStats out;
      out.rtc = fromProto(s.remote_outbound_rtp().rtc());
      out.stream = fromProto(s.remote_outbound_rtp().stream());
      out.sent = fromProto(s.remote_outbound_rtp().sent());
      out.remote_outbound = fromProto(s.remote_outbound_rtp().remote_outbound());
      return RtcStats{std::move(out)};
    }
    case P::kMediaSource: {
      RtcMediaSourceStats out;
      out.rtc = fromProto(s.media_source().rtc());
      out.source = fromProto(s.media_source().source());
      out.audio = fromProto(s.media_source().audio());
      out.video = fromProto(s.media_source().video());
      return RtcStats{std::move(out)};
    }
    case P::kMediaPlayout: {
      RtcMediaPlayoutStats out;
      out.rtc = fromProto(s.media_playout().rtc());
      out.audio_playout = fromProto(s.media_playout().audio_playout());
      return RtcStats{std::move(out)};
    }
    case P::kPeerConnection: {
      RtcPeerConnectionStats out;
      out.rtc = fromProto(s.peer_connection().rtc());
      out.pc = fromProto(s.peer_connection().pc());
      return RtcStats{std::move(out)};
    }
    case P::kDataChannel: {
      RtcDataChannelStats out;
      out.rtc = fromProto(s.data_channel().rtc());
      out.dc = fromProto(s.data_channel().dc());
      return RtcStats{std::move(out)};
    }
    case P::kTransport: {
      RtcTransportStats out;
      out.rtc = fromProto(s.transport().rtc());
      out.transport = fromProto(s.transport().transport());
      return RtcStats{std::move(out)};
    }
    case P::kCandidatePair: {
      RtcCandidatePairStats out;
      out.rtc = fromProto(s.candidate_pair().rtc());
      out.candidate_pair = fromProto(s.candidate_pair().candidate_pair());
      return RtcStats{std::move(out)};
    }
    case P::kLocalCandidate: {
      RtcLocalCandidateStats out;
      out.rtc = fromProto(s.local_candidate().rtc());
      out.candidate = fromProto(s.local_candidate().candidate());
      return RtcStats{std::move(out)};
    }
    case P::kRemoteCandidate: {
      RtcRemoteCandidateStats out;
      out.rtc = fromProto(s.remote_candidate().rtc());
      out.candidate = fromProto(s.remote_candidate().candidate());
      return RtcStats{std::move(out)};
    }
    case P::kCertificate: {
      RtcCertificateStats out;
      out.rtc = fromProto(s.certificate().rtc());
      out.certificate = fromProto(s.certificate().certificate());
      return RtcStats{std::move(out)};
    }
    case P::kStream: {
      RtcStreamStats out;
      out.rtc = fromProto(s.stream().rtc());
      out.stream = fromProto(s.stream().stream());
      return RtcStats{std::move(out)};
    }
    case P::kTrack:
      // Deprecated; fall through to default
    case P::STATS_NOT_SET:
    default: {
      // You might want to handle this differently (throw, assert, etc.)
      RtcCodecStats dummy{};
      dummy.rtc = RtcStatsData{};
      dummy.codec = CodecStats{};
      return RtcStats{std::move(dummy)};
    }
  }
}

std::vector<RtcStats> fromProto(const std::vector<proto::RtcStats>& src) {
  std::vector<RtcStats> out;
  out.reserve(src.size());
  for (const auto& s : src) {
    out.push_back(fromProto(s));
  }
  return out;
}

}  // namespace livekit
