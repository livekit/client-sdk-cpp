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

#include "livekit/data_stream.h"
#include "livekit/local_participant.h"
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

DisconnectReason toDisconnectReason(proto::DisconnectReason /*in*/) {
  // TODO: map each proto::DisconnectReason to your DisconnectReason enum
  return DisconnectReason::Unknown;
}

// --------- basic helper conversions ---------

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

RoomSidChangedEvent fromProto(const proto::RoomSidChanged &in) {
  RoomSidChangedEvent ev;
  ev.sid = in.sid();
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

UserDataPacketEvent userDataPacketFromProto(const proto::DataPacketReceived &in,
                                            RemoteParticipant *participant) {
  UserDataPacketEvent ev;
  ev.kind = static_cast<DataPacketKind>(in.kind());
  ev.participant = participant;
  ev.topic = in.user().topic();

  // Copy bytes
  const auto &owned = in.user().data();
  const auto &info = owned.data();
  if (info.data_ptr() != 0 && info.data_len() > 0) {
    auto ptr = reinterpret_cast<const std::uint8_t *>(info.data_ptr());
    auto len = static_cast<std::size_t>(info.data_len());
    ev.data.assign(ptr, ptr + len);
  } else {
    ev.data.clear();
  }

  return ev;
}

SipDtmfReceivedEvent sipDtmfFromProto(const proto::DataPacketReceived &in,
                                      RemoteParticipant *participant) {
  SipDtmfReceivedEvent ev;
  ev.participant = participant;
  ev.code = in.sip_dtmf().code();
  ev.digit = in.sip_dtmf().digit();
  return ev;
}

std::map<std::string, std::string>
toAttrMap(const proto::DataStream::Trailer &trailer) {
  std::map<std::string, std::string> out;
  for (const auto &kv : trailer.attributes()) {
    out.emplace(kv.first, kv.second);
  }
  return out;
}

TextStreamInfo makeTextInfo(const proto::DataStream::Header &header) {
  TextStreamInfo info;
  info.stream_id = header.stream_id();
  info.mime_type = header.mime_type();
  info.topic = header.topic();
  info.timestamp = header.timestamp();

  if (header.has_total_length()) {
    info.size = static_cast<std::size_t>(header.total_length());
  }

  for (const auto &kv : header.attributes()) {
    info.attributes.emplace(kv.first, kv.second);
  }

  for (const auto &id : header.text_header().attached_stream_ids()) {
    info.attachments.push_back(id);
  }

  return info;
}

ByteStreamInfo makeByteInfo(const proto::DataStream::Header &header) {
  ByteStreamInfo info;
  info.stream_id = header.stream_id();
  info.mime_type = header.mime_type();
  info.topic = header.topic();
  info.timestamp = header.timestamp();

  if (header.has_total_length()) {
    info.size = static_cast<std::size_t>(header.total_length());
  }

  for (const auto &kv : header.attributes()) {
    info.attributes.emplace(kv.first, kv.second);
  }

  info.name = header.byte_header().name();
  return info;
}

} // namespace livekit
