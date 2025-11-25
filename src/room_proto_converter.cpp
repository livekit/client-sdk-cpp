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

#include "room_proto_converter.h"

#include "room.pb.h"

namespace livekit {

// --------- enum conversions ---------

ConnectionQuality toConnectionQuality(proto::ConnectionQuality in) {
  switch (in) {
  case proto::QUALITY_POOR:
    return ConnectionQuality::Poor;
  case proto::QUALITY_GOOD:
    return ConnectionQuality::Good;
  case proto::QUALITY_EXCELLENT:
    return ConnectionQuality::Excellent;
  case proto::QUALITY_LOST:
    return ConnectionQuality::Lost;
  default:
    return ConnectionQuality::Good;
  }
}

ConnectionState toConnectionState(proto::ConnectionState in) {
  switch (in) {
  case proto::CONN_DISCONNECTED:
    return ConnectionState::Disconnected;
  case proto::CONN_CONNECTED:
    return ConnectionState::Connected;
  case proto::CONN_RECONNECTING:
    return ConnectionState::Reconnecting;
  default:
    return ConnectionState::Disconnected;
  }
}

DataPacketKind toDataPacketKind(proto::DataPacketKind in) {
  switch (in) {
  case proto::KIND_LOSSY:
    return DataPacketKind::Lossy;
  case proto::KIND_RELIABLE:
    return DataPacketKind::Reliable;
  default:
    return DataPacketKind::Reliable;
  }
}

EncryptionState toEncryptionState(proto::EncryptionState /*in*/) {
  // TODO: fill out once you have the proto::EncryptionState enum
  return EncryptionState::Unknown;
}

DisconnectReason toDisconnectReason(proto::DisconnectReason /*in*/) {
  // TODO: map each proto::DisconnectReason to your DisconnectReason enum
  return DisconnectReason::Unknown;
}

// --------- basic helper conversions ---------

ChatMessageData fromProto(const proto::ChatMessage &in) {
  ChatMessageData out;
  out.id = in.id();
  out.timestamp = in.timestamp();
  out.message = in.message();
  if (in.has_edit_timestamp()) {
    out.edit_timestamp = in.edit_timestamp();
  }
  if (in.has_deleted()) {
    out.deleted = in.deleted();
  }
  if (in.has_generated()) {
    out.generated = in.generated();
  }
  return out;
}

UserPacketData fromProto(const proto::UserPacket &in) {
  UserPacketData out;
  // TODO, double check following code is safe
  const auto &buf = in.data().data();
  auto ptr = reinterpret_cast<const std::uint8_t *>(buf.data_ptr());
  auto len = static_cast<std::size_t>(buf.data_len());
  out.data.assign(ptr, ptr + len);
  if (in.has_topic()) {
    out.topic = in.topic();
  }
  return out;
}

SipDtmfData fromProto(const proto::SipDTMF &in) {
  SipDtmfData out;
  out.code = in.code();
  if (in.has_digit()) {
    out.digit = in.digit();
  }
  return out;
}

RoomInfoData fromProto(const proto::RoomInfo &in) {
  RoomInfoData out;
  if (in.has_sid()) {
    out.sid = in.sid();
  }
  out.name = in.name();
  out.metadata = in.metadata();
  out.lossy_dc_buffered_amount_low_threshold =
      in.lossy_dc_buffered_amount_low_threshold();
  out.reliable_dc_buffered_amount_low_threshold =
      in.reliable_dc_buffered_amount_low_threshold();
  out.empty_timeout = in.empty_timeout();
  out.departure_timeout = in.departure_timeout();
  out.max_participants = in.max_participants();
  out.creation_time = in.creation_time();
  out.num_participants = in.num_participants();
  out.num_publishers = in.num_publishers();
  out.active_recording = in.active_recording();
  return out;
}

AttributeEntry fromProto(const proto::AttributesEntry &in) {
  AttributeEntry a;
  a.key = in.key();
  a.value = in.value();
  return a;
}

DataStreamHeaderData fromProto(const proto::DataStream_Header &in) {
  DataStreamHeaderData out;
  out.stream_id = in.stream_id();
  out.timestamp = in.timestamp();
  out.mime_type = in.mime_type();
  out.topic = in.topic();
  if (in.has_total_length()) {
    out.total_length = in.total_length();
  }
  for (const auto &kv : in.attributes()) {
    out.attributes.emplace(kv.first, kv.second);
  }

  // content_header oneof
  switch (in.content_header_case()) {
  case proto::DataStream_Header::kTextHeader: {
    out.content_type = DataStreamHeaderData::ContentType::Text;
    const auto &t = in.text_header();
    out.operation_type =
        static_cast<DataStreamHeaderData::OperationType>(t.operation_type());
    if (t.has_version()) {
      out.version = t.version();
    }
    if (t.has_reply_to_stream_id()) {
      out.reply_to_stream_id = t.reply_to_stream_id();
    }
    for (const auto &id : t.attached_stream_ids()) {
      out.attached_stream_ids.push_back(id);
    }
    if (t.has_generated()) {
      out.generated = t.generated();
    }
    break;
  }
  case proto::DataStream_Header::kByteHeader: {
    out.content_type = DataStreamHeaderData::ContentType::Byte;
    const auto &b = in.byte_header();
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

DataStreamChunkData fromProto(const proto::DataStream_Chunk &in) {
  DataStreamChunkData out;
  out.stream_id = in.stream_id();
  out.chunk_index = in.chunk_index();
  out.content.assign(in.content().begin(), in.content().end());
  if (in.has_version()) {
    out.version = in.version();
  }
  if (in.has_iv()) {
    out.iv.assign(in.iv().begin(), in.iv().end());
  }
  return out;
}

DataStreamTrailerData fromProto(const proto::DataStream_Trailer &in) {
  DataStreamTrailerData out;
  out.stream_id = in.stream_id();
  out.reason = in.reason();
  for (const auto &kv : in.attributes()) {
    out.attributes.emplace(kv.first, kv.second);
  }
  return out;
}

// --------- event conversions ---------

ParticipantConnectedEvent fromProto(const proto::ParticipantConnected &in) {
  ParticipantConnectedEvent ev;
  const auto &pinfo = in.info().info();
  ev.identity = pinfo.identity();
  ev.name = pinfo.name();
  ev.metadata = pinfo.metadata();
  return ev;
}

ParticipantDisconnectedEvent
fromProto(const proto::ParticipantDisconnected &in) {
  ParticipantDisconnectedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.reason = toDisconnectReason(in.disconnect_reason());
  return ev;
}

LocalTrackPublishedEvent fromProto(const proto::LocalTrackPublished &in) {
  LocalTrackPublishedEvent ev;
  ev.track_sid = in.track_sid();
  return ev;
}

LocalTrackUnpublishedEvent fromProto(const proto::LocalTrackUnpublished &in) {
  LocalTrackUnpublishedEvent ev;
  ev.publication_sid = in.publication_sid();
  return ev;
}

LocalTrackSubscribedEvent fromProto(const proto::LocalTrackSubscribed &in) {
  LocalTrackSubscribedEvent ev;
  ev.track_sid = in.track_sid();
  return ev;
}

TrackPublishedEvent fromProto(const proto::TrackPublished &in) {
  TrackPublishedEvent ev;
  ev.participant_identity = in.participant_identity();
  // OwnedTrackPublication publication = 2;
  // TODO: map publication info once you inspect OwnedTrackPublication
  // ev.publication_sid = in.publication().info().sid();
  // ev.track_name      = in.publication().info().name();
  // ev.track_kind      = ...;
  // ev.track_source    = ...;
  return ev;
}

TrackUnpublishedEvent fromProto(const proto::TrackUnpublished &in) {
  TrackUnpublishedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.publication_sid = in.publication_sid();
  return ev;
}

TrackSubscribedEvent fromProto(const proto::TrackSubscribed &in) {
  TrackSubscribedEvent ev;
  ev.participant_identity = in.participant_identity();
  // OwnedTrack track = 2;
  // TODO: map track info once you inspect OwnedTrack
  // ev.track_sid   = in.track().info().sid();
  // ev.track_name  = in.track().info().name();
  // ev.track_kind  = ...;
  // ev.track_source = ...;
  return ev;
}

TrackUnsubscribedEvent fromProto(const proto::TrackUnsubscribed &in) {
  TrackUnsubscribedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.track_sid = in.track_sid();
  return ev;
}

TrackSubscriptionFailedEvent
fromProto(const proto::TrackSubscriptionFailed &in) {
  TrackSubscriptionFailedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.track_sid = in.track_sid();
  ev.error = in.error();
  return ev;
}

TrackMutedEvent fromProto(const proto::TrackMuted &in) {
  TrackMutedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.track_sid = in.track_sid();
  return ev;
}

TrackUnmutedEvent fromProto(const proto::TrackUnmuted &in) {
  TrackUnmutedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.track_sid = in.track_sid();
  return ev;
}

ActiveSpeakersChangedEvent fromProto(const proto::ActiveSpeakersChanged &in) {
  ActiveSpeakersChangedEvent ev;
  for (const auto &id : in.participant_identities()) {
    ev.participant_identities.push_back(id);
  }
  return ev;
}

RoomMetadataChangedEvent fromProto(const proto::RoomMetadataChanged &in) {
  RoomMetadataChangedEvent ev;
  ev.metadata = in.metadata();
  return ev;
}

RoomSidChangedEvent fromProto(const proto::RoomSidChanged &in) {
  RoomSidChangedEvent ev;
  ev.sid = in.sid();
  return ev;
}

ParticipantMetadataChangedEvent
fromProto(const proto::ParticipantMetadataChanged &in) {
  ParticipantMetadataChangedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.metadata = in.metadata();
  return ev;
}

ParticipantNameChangedEvent fromProto(const proto::ParticipantNameChanged &in) {
  ParticipantNameChangedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.name = in.name();
  return ev;
}

ParticipantAttributesChangedEvent
fromProto(const proto::ParticipantAttributesChanged &in) {
  ParticipantAttributesChangedEvent ev;
  ev.participant_identity = in.participant_identity();
  for (const auto &a : in.attributes()) {
    ev.attributes.push_back(fromProto(a));
  }
  for (const auto &a : in.changed_attributes()) {
    ev.changed_attributes.push_back(fromProto(a));
  }
  return ev;
}

ParticipantEncryptionStatusChangedEvent
fromProto(const proto::ParticipantEncryptionStatusChanged &in) {
  ParticipantEncryptionStatusChangedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.is_encrypted = in.is_encrypted();
  return ev;
}

ConnectionQualityChangedEvent
fromProto(const proto::ConnectionQualityChanged &in) {
  ConnectionQualityChangedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.quality = toConnectionQuality(in.quality());
  return ev;
}

DataPacketReceivedEvent fromProto(const proto::DataPacketReceived &in) {
  DataPacketReceivedEvent ev;
  ev.kind = toDataPacketKind(in.kind());
  ev.participant_identity = in.participant_identity();

  switch (in.value_case()) {
  case proto::DataPacketReceived::kUser:
    ev.user = fromProto(in.user());
    break;
  case proto::DataPacketReceived::kSipDtmf:
    ev.sip_dtmf = fromProto(in.sip_dtmf());
    break;
  case proto::DataPacketReceived::VALUE_NOT_SET:
  default:
    break;
  }

  return ev;
}

ConnectionStateChangedEvent fromProto(const proto::ConnectionStateChanged &in) {
  ConnectionStateChangedEvent ev;
  ev.state = toConnectionState(in.state());
  return ev;
}

DisconnectedEvent fromProto(const proto::Disconnected &in) {
  DisconnectedEvent ev;
  ev.reason = toDisconnectReason(in.reason());
  return ev;
}

ReconnectingEvent fromProto(const proto::Reconnecting & /*in*/) {
  return ReconnectingEvent{};
}

ReconnectedEvent fromProto(const proto::Reconnected & /*in*/) {
  return ReconnectedEvent{};
}

RoomEosEvent fromProto(const proto::RoomEOS & /*in*/) { return RoomEosEvent{}; }

DataStreamHeaderReceivedEvent
fromProto(const proto::DataStreamHeaderReceived &in) {
  DataStreamHeaderReceivedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.header = fromProto(in.header());
  return ev;
}

DataStreamChunkReceivedEvent
fromProto(const proto::DataStreamChunkReceived &in) {
  DataStreamChunkReceivedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.chunk = fromProto(in.chunk());
  return ev;
}

DataStreamTrailerReceivedEvent
fromProto(const proto::DataStreamTrailerReceived &in) {
  DataStreamTrailerReceivedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.trailer = fromProto(in.trailer());
  return ev;
}

DataChannelBufferedAmountLowThresholdChangedEvent
fromProto(const proto::DataChannelBufferedAmountLowThresholdChanged &in) {
  DataChannelBufferedAmountLowThresholdChangedEvent ev;
  ev.kind = toDataPacketKind(in.kind());
  ev.threshold = in.threshold();
  return ev;
}

ByteStreamOpenedEvent fromProto(const proto::ByteStreamOpened &in) {
  ByteStreamOpenedEvent ev;
  // TODO: map reader handle once OwnedByteStreamReader is known
  // ev.reader_handle = in.reader().handle().id();
  ev.participant_identity = in.participant_identity();
  return ev;
}

TextStreamOpenedEvent fromProto(const proto::TextStreamOpened &in) {
  TextStreamOpenedEvent ev;
  // TODO: map reader handle once OwnedTextStreamReader is known
  // ev.reader_handle = in.reader().handle().id();
  ev.participant_identity = in.participant_identity();
  return ev;
}

RoomUpdatedEvent roomUpdatedFromProto(const proto::RoomInfo &in) {
  RoomUpdatedEvent ev;
  ev.info = fromProto(in);
  return ev;
}

RoomMovedEvent roomMovedFromProto(const proto::RoomInfo &in) {
  RoomMovedEvent ev;
  ev.info = fromProto(in);
  return ev;
}

ParticipantsUpdatedEvent fromProto(const proto::ParticipantsUpdated &in) {
  ParticipantsUpdatedEvent ev;
  // We only know that it has ParticipantInfo participants = 1;
  // TODO: fill real identities once you inspect proto::ParticipantInfo
  for (const auto &p : in.participants()) {
    ev.participant_identities.push_back(p.identity());
  }
  return ev;
}

E2eeStateChangedEvent fromProto(const proto::E2eeStateChanged &in) {
  E2eeStateChangedEvent ev;
  ev.participant_identity = in.participant_identity();
  ev.state = toEncryptionState(in.state());
  return ev;
}

ChatMessageReceivedEvent fromProto(const proto::ChatMessageReceived &in) {
  ChatMessageReceivedEvent ev;
  ev.message = fromProto(in.message());
  ev.participant_identity = in.participant_identity();
  return ev;
}

// ---------------- Room Options ----------------

proto::AudioEncoding toProto(const AudioEncodingOptions &in) {
  proto::AudioEncoding msg;
  msg.set_max_bitrate(in.max_bitrate);
  return msg;
}

AudioEncodingOptions fromProto(const proto::AudioEncoding &in) {
  AudioEncodingOptions out;
  out.max_bitrate = in.max_bitrate();
  return out;
}

proto::VideoEncoding toProto(const VideoEncodingOptions &in) {
  proto::VideoEncoding msg;
  msg.set_max_bitrate(in.max_bitrate);
  msg.set_max_framerate(in.max_framerate);
  return msg;
}

VideoEncodingOptions fromProto(const proto::VideoEncoding &in) {
  VideoEncodingOptions out;
  out.max_bitrate = in.max_bitrate();
  out.max_framerate = in.max_framerate();
  return out;
}

proto::TrackPublishOptions toProto(const TrackPublishOptions &in) {
  proto::TrackPublishOptions msg;
  if (in.video_encoding) {
    msg.mutable_video_encoding()->CopyFrom(toProto(*in.video_encoding));
  }
  if (in.audio_encoding) {
    msg.mutable_audio_encoding()->CopyFrom(toProto(*in.audio_encoding));
  }
  if (in.video_codec) {
    msg.set_video_codec(static_cast<proto::VideoCodec>(*in.video_codec));
  }
  if (in.dtx) {
    msg.set_dtx(*in.dtx);
  }
  if (in.red) {
    msg.set_red(*in.red);
  }
  if (in.simulcast) {
    msg.set_simulcast(*in.simulcast);
  }
  if (in.source) {
    msg.set_source(static_cast<proto::TrackSource>(*in.source));
  }
  if (in.stream) {
    msg.set_stream(*in.stream);
  }
  if (in.preconnect_buffer) {
    msg.set_preconnect_buffer(*in.preconnect_buffer);
  }
  return msg;
}

TrackPublishOptions fromProto(const proto::TrackPublishOptions &in) {
  TrackPublishOptions out;
  if (in.has_video_encoding()) {
    out.video_encoding = fromProto(in.video_encoding());
  }
  if (in.has_audio_encoding()) {
    out.audio_encoding = fromProto(in.audio_encoding());
  }
  if (in.has_video_codec()) {
    out.video_codec = static_cast<VideoCodec>(in.video_codec());
  }
  if (in.has_dtx()) {
    out.dtx = in.dtx();
  }
  if (in.has_red()) {
    out.red = in.red();
  }
  if (in.has_simulcast()) {
    out.simulcast = in.simulcast();
  }
  if (in.has_source()) {
    out.source = static_cast<TrackSource>(in.source());
  }
  if (in.has_stream()) {
    out.stream = in.stream();
  }
  if (in.has_preconnect_buffer()) {
    out.preconnect_buffer = in.preconnect_buffer();
  }
  return out;
}

proto::TranscriptionSegment toProto(const TranscriptionSegment &in) {
  proto::TranscriptionSegment msg;
  msg.set_id(in.id);
  msg.set_text(in.text);
  msg.set_start_time(in.start_time);
  msg.set_end_time(in.end_time);
  msg.set_final(in.final);
  msg.set_language(in.language);
  return msg;
}

TranscriptionSegment fromProto(const proto::TranscriptionSegment &in) {
  TranscriptionSegment out;
  out.id = in.id();
  out.text = in.text();
  out.start_time = in.start_time();
  out.end_time = in.end_time();
  out.final = in.final();
  out.language = in.language();
  return out;
}

proto::TranscriptionReceived toProto(const Transcription &in) {
  proto::TranscriptionReceived msg;
  if (in.participant_identity) {
    msg.set_participant_identity(*in.participant_identity);
  }
  if (in.track_sid) {
    msg.set_track_sid(*in.track_sid);
  }
  for (const auto &seg : in.segments) {
    auto *pseg = msg.add_segments();
    pseg->CopyFrom(toProto(seg));
  }
  return msg;
}

Transcription fromProto(const proto::TranscriptionReceived &in) {
  Transcription out;
  if (in.has_participant_identity()) {
    out.participant_identity = in.participant_identity();
  }
  if (in.has_track_sid()) {
    out.track_sid = in.track_sid();
  }
  out.segments.reserve(in.segments_size());
  for (const auto &pseg : in.segments()) {
    out.segments.push_back(fromProto(pseg));
  }
  return out;
}

} // namespace livekit
