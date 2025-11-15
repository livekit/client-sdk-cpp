#include "room_event_converter.h"

#include "room.pb.h"

namespace livekit {

// --------- enum conversions ---------

ConnectionQuality toConnectionQuality(proto::ConnectionQuality src) {
  switch (src) {
    case proto::QUALITY_POOR:      return ConnectionQuality::Poor;
    case proto::QUALITY_GOOD:      return ConnectionQuality::Good;
    case proto::QUALITY_EXCELLENT: return ConnectionQuality::Excellent;
    case proto::QUALITY_LOST:      return ConnectionQuality::Lost;
    default:                       return ConnectionQuality::Good;
  }
}

ConnectionState toConnectionState(proto::ConnectionState src) {
  switch (src) {
    case proto::CONN_DISCONNECTED: return ConnectionState::Disconnected;
    case proto::CONN_CONNECTED:    return ConnectionState::Connected;
    case proto::CONN_RECONNECTING: return ConnectionState::Reconnecting;
    default:                       return ConnectionState::Disconnected;
  }
}

DataPacketKind toDataPacketKind(proto::DataPacketKind src) {
  switch (src) {
    case proto::KIND_LOSSY:    return DataPacketKind::Lossy;
    case proto::KIND_RELIABLE: return DataPacketKind::Reliable;
    default:                   return DataPacketKind::Reliable;
  }
}

EncryptionState toEncryptionState(proto::EncryptionState /*src*/) {
  // TODO: fill out once you have the proto::EncryptionState enum
  return EncryptionState::Unknown;
}

DisconnectReason toDisconnectReason(proto::DisconnectReason /*src*/) {
  // TODO: map each proto::DisconnectReason to your DisconnectReason enum
  return DisconnectReason::Unknown;
}

// --------- basic helper conversions ---------

TranscriptionSegmentData fromProto(const proto::TranscriptionSegment& src) {
  TranscriptionSegmentData out;
  out.id         = src.id();
  out.text       = src.text();
  out.start_time = src.start_time();
  out.end_time   = src.end_time();
  out.is_final   = src.final();
  out.language   = src.language();
  return out;
}

ChatMessageData fromProto(const proto::ChatMessage& src) {
  ChatMessageData out;
  out.id         = src.id();
  out.timestamp  = src.timestamp();
  out.message    = src.message();
  if (src.has_edit_timestamp()) {
    out.edit_timestamp = src.edit_timestamp();
  }
  if (src.has_deleted()) {
    out.deleted = src.deleted();
  }
  if (src.has_generated()) {
    out.generated = src.generated();
  }
  return out;
}

UserPacketData fromProto(const proto::UserPacket& src) {
  UserPacketData out;
  // TODO, double check following code is safe
  const auto& buf = src.data().data();
  auto ptr = reinterpret_cast<const std::uint8_t*>(buf.data_ptr());
  auto len = static_cast<std::size_t>(buf.data_len());
  out.data.assign(ptr, ptr + len);
  if (src.has_topic()) {
    out.topic = src.topic();
  }
  return out;
}

SipDtmfData fromProto(const proto::SipDTMF& src) {
  SipDtmfData out;
  out.code = src.code();
  if (src.has_digit()) {
    out.digit = src.digit();
  }
  return out;
}

RoomInfoData fromProto(const proto::RoomInfo& src) {
  RoomInfoData out;
  if (src.has_sid()) {
    out.sid = src.sid();
  }
  out.name                               = src.name();
  out.metadata                           = src.metadata();
  out.lossy_dc_buffered_amount_low_threshold    = src.lossy_dc_buffered_amount_low_threshold();
  out.reliable_dc_buffered_amount_low_threshold = src.reliable_dc_buffered_amount_low_threshold();
  out.empty_timeout                      = src.empty_timeout();
  out.departure_timeout                  = src.departure_timeout();
  out.max_participants                   = src.max_participants();
  out.creation_time                      = src.creation_time();
  out.num_participants                   = src.num_participants();
  out.num_publishers                     = src.num_publishers();
  out.active_recording                   = src.active_recording();
  return out;
}

AttributeEntry fromProto(const proto::AttributesEntry& src) {
  AttributeEntry a;
  a.key   = src.key();
  a.value = src.value();
  return a;
}

DataStreamHeaderData fromProto(const proto::DataStream_Header& src) {
  DataStreamHeaderData out;
  out.stream_id  = src.stream_id();
  out.timestamp  = src.timestamp();
  out.mime_type  = src.mime_type();
  out.topic      = src.topic();
  if (src.has_total_length()) {
    out.total_length = src.total_length();
  }
  for (const auto& kv : src.attributes()) {
    out.attributes.emplace(kv.first, kv.second);
  }

  // content_header oneof
  switch (src.content_header_case()) {
    case proto::DataStream_Header::kTextHeader: {
      out.content_type = DataStreamHeaderData::ContentType::Text;
      const auto& t = src.text_header();
      out.operation_type = static_cast<DataStreamHeaderData::OperationType>(t.operation_type());
      if (t.has_version()) {
        out.version = t.version();
      }
      if (t.has_reply_to_stream_id()) {
        out.reply_to_stream_id = t.reply_to_stream_id();
      }
      for (const auto& id : t.attached_stream_ids()) {
        out.attached_stream_ids.push_back(id);
      }
      if (t.has_generated()) {
        out.generated = t.generated();
      }
      break;
    }
    case proto::DataStream_Header::kByteHeader: {
      out.content_type = DataStreamHeaderData::ContentType::Byte;
      const auto& b = src.byte_header();
      out.name = b.name();
      break;
    }
    case proto::DataStream_Header::CONTENT_HEADER_NOT_SET:
    default:
      out.content_type = DataStreamHeaderData::ContentType::None;
      break;
  }

  return out;
}

DataStreamChunkData fromProto(const proto::DataStream_Chunk& src) {
  DataStreamChunkData out;
  out.stream_id   = src.stream_id();
  out.chunk_index = src.chunk_index();
  out.content.assign(src.content().begin(), src.content().end());
  if (src.has_version()) {
    out.version = src.version();
  }
  if (src.has_iv()) {
    out.iv.assign(src.iv().begin(), src.iv().end());
  }
  return out;
}

DataStreamTrailerData fromProto(const proto::DataStream_Trailer& src) {
  DataStreamTrailerData out;
  out.stream_id = src.stream_id();
  out.reason    = src.reason();
  for (const auto& kv : src.attributes()) {
    out.attributes.emplace(kv.first, kv.second);
  }
  return out;
}

// --------- event conversions ---------

ParticipantConnectedEvent fromProto(const proto::ParticipantConnected& src) {
  ParticipantConnectedEvent ev;
  // src.info() is OwnedParticipant; you can fill more fields once you inspect it.
  // For now, leave metadata/name/identity as TODO.
  // TODO: map src.info().info().identity(), name(), metadata(), etc.
  return ev;
}

ParticipantDisconnectedEvent fromProto(const proto::ParticipantDisconnected& src) {
  ParticipantDisconnectedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.reason = toDisconnectReason(src.disconnect_reason());
  return ev;
}

LocalTrackPublishedEvent fromProto(const proto::LocalTrackPublished& src) {
  LocalTrackPublishedEvent ev;
  ev.track_sid = src.track_sid();
  return ev;
}

LocalTrackUnpublishedEvent fromProto(const proto::LocalTrackUnpublished& src) {
  LocalTrackUnpublishedEvent ev;
  ev.publication_sid = src.publication_sid();
  return ev;
}

LocalTrackSubscribedEvent fromProto(const proto::LocalTrackSubscribed& src) {
  LocalTrackSubscribedEvent ev;
  ev.track_sid = src.track_sid();
  return ev;
}

TrackPublishedEvent fromProto(const proto::TrackPublished& src) {
  TrackPublishedEvent ev;
  ev.participant_identity = src.participant_identity();
  // OwnedTrackPublication publication = 2;
  // TODO: map publication info once you inspect OwnedTrackPublication
  // ev.publication_sid = src.publication().info().sid();
  // ev.track_name      = src.publication().info().name();
  // ev.track_kind      = ...;
  // ev.track_source    = ...;
  return ev;
}

TrackUnpublishedEvent fromProto(const proto::TrackUnpublished& src) {
  TrackUnpublishedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.publication_sid      = src.publication_sid();
  return ev;
}

TrackSubscribedEvent fromProto(const proto::TrackSubscribed& src) {
  TrackSubscribedEvent ev;
  ev.participant_identity = src.participant_identity();
  // OwnedTrack track = 2;
  // TODO: map track info once you inspect OwnedTrack
  // ev.track_sid   = src.track().info().sid();
  // ev.track_name  = src.track().info().name();
  // ev.track_kind  = ...;
  // ev.track_source = ...;
  return ev;
}

TrackUnsubscribedEvent fromProto(const proto::TrackUnsubscribed& src) {
  TrackUnsubscribedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.track_sid            = src.track_sid();
  return ev;
}

TrackSubscriptionFailedEvent fromProto(const proto::TrackSubscriptionFailed& src) {
  TrackSubscriptionFailedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.track_sid            = src.track_sid();
  ev.error                = src.error();
  return ev;
}

TrackMutedEvent fromProto(const proto::TrackMuted& src) {
  TrackMutedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.track_sid            = src.track_sid();
  return ev;
}

TrackUnmutedEvent fromProto(const proto::TrackUnmuted& src) {
  TrackUnmutedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.track_sid            = src.track_sid();
  return ev;
}

ActiveSpeakersChangedEvent fromProto(const proto::ActiveSpeakersChanged& src) {
  ActiveSpeakersChangedEvent ev;
  for (const auto& id : src.participant_identities()) {
    ev.participant_identities.push_back(id);
  }
  return ev;
}

RoomMetadataChangedEvent fromProto(const proto::RoomMetadataChanged& src) {
  RoomMetadataChangedEvent ev;
  ev.metadata = src.metadata();
  return ev;
}

RoomSidChangedEvent fromProto(const proto::RoomSidChanged& src) {
  RoomSidChangedEvent ev;
  ev.sid = src.sid();
  return ev;
}

ParticipantMetadataChangedEvent fromProto(const proto::ParticipantMetadataChanged& src) {
  ParticipantMetadataChangedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.metadata             = src.metadata();
  return ev;
}

ParticipantNameChangedEvent fromProto(const proto::ParticipantNameChanged& src) {
  ParticipantNameChangedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.name                 = src.name();
  return ev;
}

ParticipantAttributesChangedEvent fromProto(const proto::ParticipantAttributesChanged& src) {
  ParticipantAttributesChangedEvent ev;
  ev.participant_identity = src.participant_identity();
  for (const auto& a : src.attributes()) {
    ev.attributes.push_back(fromProto(a));
  }
  for (const auto& a : src.changed_attributes()) {
    ev.changed_attributes.push_back(fromProto(a));
  }
  return ev;
}

ParticipantEncryptionStatusChangedEvent
fromProto(const proto::ParticipantEncryptionStatusChanged& src) {
  ParticipantEncryptionStatusChangedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.is_encrypted         = src.is_encrypted();
  return ev;
}

ConnectionQualityChangedEvent fromProto(const proto::ConnectionQualityChanged& src) {
  ConnectionQualityChangedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.quality              = toConnectionQuality(src.quality());
  return ev;
}

DataPacketReceivedEvent fromProto(const proto::DataPacketReceived& src) {
  DataPacketReceivedEvent ev;
  ev.kind                = toDataPacketKind(src.kind());
  ev.participant_identity = src.participant_identity();

  switch (src.value_case()) {
    case proto::DataPacketReceived::kUser:
      ev.user = fromProto(src.user());
      break;
    case proto::DataPacketReceived::kSipDtmf:
      ev.sip_dtmf = fromProto(src.sip_dtmf());
      break;
    case proto::DataPacketReceived::VALUE_NOT_SET:
    default:
      break;
  }

  return ev;
}

TranscriptionReceivedEvent fromProto(const proto::TranscriptionReceived& src) {
  TranscriptionReceivedEvent ev;
  if (src.has_participant_identity()) {
    ev.participant_identity = src.participant_identity();
  }
  if (src.has_track_sid()) {
    ev.track_sid = src.track_sid();
  }
  for (const auto& seg : src.segments()) {
    ev.segments.push_back(fromProto(seg));
  }
  return ev;
}

ConnectionStateChangedEvent fromProto(const proto::ConnectionStateChanged& src) {
  ConnectionStateChangedEvent ev;
  ev.state = toConnectionState(src.state());
  return ev;
}

DisconnectedEvent fromProto(const proto::Disconnected& src) {
  DisconnectedEvent ev;
  ev.reason = toDisconnectReason(src.reason());
  return ev;
}

ReconnectingEvent fromProto(const proto::Reconnecting& /*src*/) {
  return ReconnectingEvent{};
}

ReconnectedEvent fromProto(const proto::Reconnected& /*src*/) {
  return ReconnectedEvent{};
}

RoomEosEvent fromProto(const proto::RoomEOS& /*src*/) {
  return RoomEosEvent{};
}

DataStreamHeaderReceivedEvent fromProto(const proto::DataStreamHeaderReceived& src) {
  DataStreamHeaderReceivedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.header = fromProto(src.header());
  return ev;
}

DataStreamChunkReceivedEvent fromProto(const proto::DataStreamChunkReceived& src) {
  DataStreamChunkReceivedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.chunk = fromProto(src.chunk());
  return ev;
}

DataStreamTrailerReceivedEvent fromProto(const proto::DataStreamTrailerReceived& src) {
  DataStreamTrailerReceivedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.trailer = fromProto(src.trailer());
  return ev;
}

DataChannelBufferedAmountLowThresholdChangedEvent
fromProto(const proto::DataChannelBufferedAmountLowThresholdChanged& src) {
  DataChannelBufferedAmountLowThresholdChangedEvent ev;
  ev.kind      = toDataPacketKind(src.kind());
  ev.threshold = src.threshold();
  return ev;
}

ByteStreamOpenedEvent fromProto(const proto::ByteStreamOpened& src) {
  ByteStreamOpenedEvent ev;
  // TODO: map reader handle once OwnedByteStreamReader is known
  // ev.reader_handle = src.reader().handle().id();
  ev.participant_identity = src.participant_identity();
  return ev;
}

TextStreamOpenedEvent fromProto(const proto::TextStreamOpened& src) {
  TextStreamOpenedEvent ev;
  // TODO: map reader handle once OwnedTextStreamReader is known
  // ev.reader_handle = src.reader().handle().id();
  ev.participant_identity = src.participant_identity();
  return ev;
}

RoomUpdatedEvent roomUpdatedFromProto(const proto::RoomInfo& src) {
  RoomUpdatedEvent ev;
  ev.info = fromProto(src);
  return ev;
}

RoomMovedEvent roomMovedFromProto(const proto::RoomInfo& src) {
  RoomMovedEvent ev;
  ev.info = fromProto(src);
  return ev;
}

ParticipantsUpdatedEvent fromProto(const proto::ParticipantsUpdated& src) {
  ParticipantsUpdatedEvent ev;
  // We only know that it has ParticipantInfo participants = 1;
  // TODO: fill real identities once you inspect proto::ParticipantInfo
  for (const auto& p : src.participants()) {
    ev.participant_identities.push_back(p.identity());
  }
  return ev;
}

E2eeStateChangedEvent fromProto(const proto::E2eeStateChanged& src) {
  E2eeStateChangedEvent ev;
  ev.participant_identity = src.participant_identity();
  ev.state                = toEncryptionState(src.state());
  return ev;
}

ChatMessageReceivedEvent fromProto(const proto::ChatMessageReceived& src) {
  ChatMessageReceivedEvent ev;
  ev.message             = fromProto(src.message());
  ev.participant_identity = src.participant_identity();
  return ev;
}

}  // namespace livekit
