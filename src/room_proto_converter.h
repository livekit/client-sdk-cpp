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

#include "livekit/room_delegate.h"
#include "room.pb.h"

#include <string>

namespace livekit {

enum class RpcErrorCode;
class RemoteParticipant;

// --------- basic helper conversions ---------

ConnectionQuality toConnectionQuality(proto::ConnectionQuality in);
ConnectionState toConnectionState(proto::ConnectionState in);
DataPacketKind toDataPacketKind(proto::DataPacketKind in);
DisconnectReason toDisconnectReason(proto::DisconnectReason in);

ChatMessageData fromProto(const proto::ChatMessage &in);
UserPacketData fromProto(const proto::UserPacket &in);
SipDtmfData fromProto(const proto::SipDTMF &in);
RoomInfoData fromProto(const proto::RoomInfo &in);

DataStreamHeaderData fromProto(const proto::DataStream_Header &in);
DataStreamChunkData fromProto(const proto::DataStream_Chunk &in);
DataStreamTrailerData fromProto(const proto::DataStream_Trailer &in);

// --------- event conversions (RoomEvent.oneof message) ---------

RoomSidChangedEvent fromProto(const proto::RoomSidChanged &in);

ConnectionStateChangedEvent fromProto(const proto::ConnectionStateChanged &in);
DisconnectedEvent fromProto(const proto::Disconnected &in);
ReconnectingEvent fromProto(const proto::Reconnecting &in);
ReconnectedEvent fromProto(const proto::Reconnected &in);
RoomEosEvent fromProto(const proto::RoomEOS &in);

DataStreamHeaderReceivedEvent
fromProto(const proto::DataStreamHeaderReceived &in);
DataStreamChunkReceivedEvent
fromProto(const proto::DataStreamChunkReceived &in);
DataStreamTrailerReceivedEvent
fromProto(const proto::DataStreamTrailerReceived &in);

DataChannelBufferedAmountLowThresholdChangedEvent
fromProto(const proto::DataChannelBufferedAmountLowThresholdChanged &in);

ByteStreamOpenedEvent fromProto(const proto::ByteStreamOpened &in);
TextStreamOpenedEvent fromProto(const proto::TextStreamOpened &in);

RoomUpdatedEvent
roomUpdatedFromProto(const proto::RoomInfo &in);              // room_updated
RoomMovedEvent roomMovedFromProto(const proto::RoomInfo &in); // moved

ChatMessageReceivedEvent fromProto(const proto::ChatMessageReceived &in);

// --------- room options conversions ---------

proto::AudioEncoding toProto(const AudioEncodingOptions &in);
AudioEncodingOptions fromProto(const proto::AudioEncoding &in);

proto::VideoEncoding toProto(const VideoEncodingOptions &in);
VideoEncodingOptions fromProto(const proto::VideoEncoding &in);

proto::TrackPublishOptions toProto(const TrackPublishOptions &in);
TrackPublishOptions fromProto(const proto::TrackPublishOptions &in);

// --------- room transcription conversions ---------

proto::TranscriptionSegment toProto(const TranscriptionSegment &in);
TranscriptionSegment fromProto(const proto::TranscriptionSegment &in);

proto::TranscriptionReceived toProto(const TranscriptionReceivedEvent &in);
Transcription fromProto(const proto::TranscriptionReceived &in);

// --------- room Data Packet conversions ---------

UserDataPacketEvent userDataPacketFromProto(const proto::DataPacketReceived &in,
                                            RemoteParticipant *participant);

SipDtmfReceivedEvent sipDtmfFromProto(const proto::DataPacketReceived &in,
                                      RemoteParticipant *participant);

} // namespace livekit
