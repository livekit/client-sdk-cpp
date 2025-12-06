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

// --------- basic helper conversions ---------

ConnectionQuality toConnectionQuality(proto::ConnectionQuality in);
ConnectionState toConnectionState(proto::ConnectionState in);
DataPacketKind toDataPacketKind(proto::DataPacketKind in);
EncryptionState toEncryptionState(proto::EncryptionState in);
DisconnectReason toDisconnectReason(proto::DisconnectReason in);

ChatMessageData fromProto(const proto::ChatMessage &in);
UserPacketData fromProto(const proto::UserPacket &in);
SipDtmfData fromProto(const proto::SipDTMF &in);
RoomInfoData fromProto(const proto::RoomInfo &in);
AttributeEntry fromProto(const proto::AttributesEntry &in);

DataStreamHeaderData fromProto(const proto::DataStream_Header &in);
DataStreamChunkData fromProto(const proto::DataStream_Chunk &in);
DataStreamTrailerData fromProto(const proto::DataStream_Trailer &in);

// --------- event conversions (RoomEvent.oneof message) ---------

ParticipantConnectedEvent fromProto(const proto::ParticipantConnected &in);
ParticipantDisconnectedEvent
fromProto(const proto::ParticipantDisconnected &in);

LocalTrackPublishedEvent fromProto(const proto::LocalTrackPublished &in);
LocalTrackUnpublishedEvent fromProto(const proto::LocalTrackUnpublished &in);
LocalTrackSubscribedEvent fromProto(const proto::LocalTrackSubscribed &in);

TrackPublishedEvent fromProto(const proto::TrackPublished &in);
TrackUnpublishedEvent fromProto(const proto::TrackUnpublished &in);
TrackUnsubscribedEvent fromProto(const proto::TrackUnsubscribed &in);
TrackSubscriptionFailedEvent
fromProto(const proto::TrackSubscriptionFailed &in);
TrackMutedEvent fromProto(const proto::TrackMuted &in);
TrackUnmutedEvent fromProto(const proto::TrackUnmuted &in);

ActiveSpeakersChangedEvent fromProto(const proto::ActiveSpeakersChanged &in);

RoomMetadataChangedEvent fromProto(const proto::RoomMetadataChanged &in);
RoomSidChangedEvent fromProto(const proto::RoomSidChanged &in);

ParticipantMetadataChangedEvent
fromProto(const proto::ParticipantMetadataChanged &in);
ParticipantNameChangedEvent fromProto(const proto::ParticipantNameChanged &in);
ParticipantAttributesChangedEvent
fromProto(const proto::ParticipantAttributesChanged &in);
ParticipantEncryptionStatusChangedEvent
fromProto(const proto::ParticipantEncryptionStatusChanged &in);

ConnectionQualityChangedEvent
fromProto(const proto::ConnectionQualityChanged &in);

DataPacketReceivedEvent fromProto(const proto::DataPacketReceived &in);

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

ParticipantsUpdatedEvent fromProto(const proto::ParticipantsUpdated &in);
E2eeStateChangedEvent fromProto(const proto::E2eeStateChanged &in);
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

proto::TranscriptionReceived toProto(const Transcription &in);
Transcription fromProto(const proto::TranscriptionReceived &in);

} // namespace livekit
