/*
 * Copyright 2023 LiveKit
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

namespace livekit {

// --------- basic helper conversions ---------

ConnectionQuality toConnectionQuality(proto::ConnectionQuality src);
ConnectionState toConnectionState(proto::ConnectionState src);
DataPacketKind toDataPacketKind(proto::DataPacketKind src);
EncryptionState toEncryptionState(proto::EncryptionState src);
DisconnectReason toDisconnectReason(proto::DisconnectReason src);

TranscriptionSegmentData fromProto(const proto::TranscriptionSegment &src);
ChatMessageData fromProto(const proto::ChatMessage &src);
UserPacketData fromProto(const proto::UserPacket &src);
SipDtmfData fromProto(const proto::SipDTMF &src);
RoomInfoData fromProto(const proto::RoomInfo &src);
AttributeEntry fromProto(const proto::AttributesEntry &src);

DataStreamHeaderData fromProto(const proto::DataStream_Header &src);
DataStreamChunkData fromProto(const proto::DataStream_Chunk &src);
DataStreamTrailerData fromProto(const proto::DataStream_Trailer &src);

// --------- event conversions (RoomEvent.oneof message) ---------

ParticipantConnectedEvent fromProto(const proto::ParticipantConnected &src);
ParticipantDisconnectedEvent
fromProto(const proto::ParticipantDisconnected &src);

LocalTrackPublishedEvent fromProto(const proto::LocalTrackPublished &src);
LocalTrackUnpublishedEvent fromProto(const proto::LocalTrackUnpublished &src);
LocalTrackSubscribedEvent fromProto(const proto::LocalTrackSubscribed &src);

TrackPublishedEvent fromProto(const proto::TrackPublished &src);
TrackUnpublishedEvent fromProto(const proto::TrackUnpublished &src);
TrackSubscribedEvent fromProto(const proto::TrackSubscribed &src);
TrackUnsubscribedEvent fromProto(const proto::TrackUnsubscribed &src);
TrackSubscriptionFailedEvent
fromProto(const proto::TrackSubscriptionFailed &src);
TrackMutedEvent fromProto(const proto::TrackMuted &src);
TrackUnmutedEvent fromProto(const proto::TrackUnmuted &src);

ActiveSpeakersChangedEvent fromProto(const proto::ActiveSpeakersChanged &src);

RoomMetadataChangedEvent fromProto(const proto::RoomMetadataChanged &src);
RoomSidChangedEvent fromProto(const proto::RoomSidChanged &src);

ParticipantMetadataChangedEvent
fromProto(const proto::ParticipantMetadataChanged &src);
ParticipantNameChangedEvent fromProto(const proto::ParticipantNameChanged &src);
ParticipantAttributesChangedEvent
fromProto(const proto::ParticipantAttributesChanged &src);
ParticipantEncryptionStatusChangedEvent
fromProto(const proto::ParticipantEncryptionStatusChanged &src);

ConnectionQualityChangedEvent
fromProto(const proto::ConnectionQualityChanged &src);

DataPacketReceivedEvent fromProto(const proto::DataPacketReceived &src);
TranscriptionReceivedEvent fromProto(const proto::TranscriptionReceived &src);

ConnectionStateChangedEvent fromProto(const proto::ConnectionStateChanged &src);
DisconnectedEvent fromProto(const proto::Disconnected &src);
ReconnectingEvent fromProto(const proto::Reconnecting &src);
ReconnectedEvent fromProto(const proto::Reconnected &src);
RoomEosEvent fromProto(const proto::RoomEOS &src);

DataStreamHeaderReceivedEvent
fromProto(const proto::DataStreamHeaderReceived &src);
DataStreamChunkReceivedEvent
fromProto(const proto::DataStreamChunkReceived &src);
DataStreamTrailerReceivedEvent
fromProto(const proto::DataStreamTrailerReceived &src);

DataChannelBufferedAmountLowThresholdChangedEvent
fromProto(const proto::DataChannelBufferedAmountLowThresholdChanged &src);

ByteStreamOpenedEvent fromProto(const proto::ByteStreamOpened &src);
TextStreamOpenedEvent fromProto(const proto::TextStreamOpened &src);

RoomUpdatedEvent
roomUpdatedFromProto(const proto::RoomInfo &src);              // room_updated
RoomMovedEvent roomMovedFromProto(const proto::RoomInfo &src); // moved

ParticipantsUpdatedEvent fromProto(const proto::ParticipantsUpdated &src);
E2eeStateChangedEvent fromProto(const proto::E2eeStateChanged &src);
ChatMessageReceivedEvent fromProto(const proto::ChatMessageReceived &src);

} // namespace livekit
