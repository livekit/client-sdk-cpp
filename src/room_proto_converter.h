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

#include <string>

#include "livekit/export.h"
#include "livekit/room_event_types.h"
#include "room.pb.h"

namespace livekit {

enum class RpcErrorCode;
class RemoteParticipant;
struct ByteStreamInfo;
struct TextStreamInfo;

// All declarations below are tagged LIVEKIT_INTERNAL_API: they are NOT part of
// the public SDK ABI, but must be exported so that the in-tree test binaries
// (which include this header directly) can resolve them against liblivekit.

// --------- basic helper conversions ---------

LIVEKIT_INTERNAL_API ConnectionQuality toConnectionQuality(proto::ConnectionQuality in);
LIVEKIT_INTERNAL_API ConnectionState toConnectionState(proto::ConnectionState in);
LIVEKIT_INTERNAL_API DataPacketKind toDataPacketKind(proto::DataPacketKind in);
LIVEKIT_INTERNAL_API DisconnectReason toDisconnectReason(proto::DisconnectReason in);

LIVEKIT_INTERNAL_API UserPacketData fromProto(const proto::UserPacket& in);
LIVEKIT_INTERNAL_API SipDtmfData fromProto(const proto::SipDTMF& in);
LIVEKIT_INTERNAL_API RoomInfoData fromProto(const proto::RoomInfo& in);

LIVEKIT_INTERNAL_API DataStreamHeaderData fromProto(const proto::DataStream_Header& in);
LIVEKIT_INTERNAL_API DataStreamChunkData fromProto(const proto::DataStream_Chunk& in);
LIVEKIT_INTERNAL_API DataStreamTrailerData fromProto(const proto::DataStream_Trailer& in);

// --------- event conversions (RoomEvent.oneof message) ---------

LIVEKIT_INTERNAL_API RoomSidChangedEvent fromProto(const proto::RoomSidChanged& in);

LIVEKIT_INTERNAL_API ConnectionStateChangedEvent fromProto(const proto::ConnectionStateChanged& in);
LIVEKIT_INTERNAL_API DisconnectedEvent fromProto(const proto::Disconnected& in);
LIVEKIT_INTERNAL_API ReconnectingEvent fromProto(const proto::Reconnecting& in);
LIVEKIT_INTERNAL_API ReconnectedEvent fromProto(const proto::Reconnected& in);
LIVEKIT_INTERNAL_API RoomEosEvent fromProto(const proto::RoomEOS& in);

LIVEKIT_INTERNAL_API DataStreamHeaderReceivedEvent fromProto(const proto::DataStreamHeaderReceived& in);
LIVEKIT_INTERNAL_API DataStreamChunkReceivedEvent fromProto(const proto::DataStreamChunkReceived& in);
LIVEKIT_INTERNAL_API DataStreamTrailerReceivedEvent fromProto(const proto::DataStreamTrailerReceived& in);

LIVEKIT_INTERNAL_API DataChannelBufferedAmountLowThresholdChangedEvent fromProto(
    const proto::DataChannelBufferedAmountLowThresholdChanged& in);

LIVEKIT_INTERNAL_API ByteStreamOpenedEvent fromProto(const proto::ByteStreamOpened& in);
LIVEKIT_INTERNAL_API TextStreamOpenedEvent fromProto(const proto::TextStreamOpened& in);

LIVEKIT_INTERNAL_API RoomUpdatedEvent roomUpdatedFromProto(const proto::RoomInfo& in); // room_updated
LIVEKIT_INTERNAL_API RoomMovedEvent roomMovedFromProto(const proto::RoomInfo& in);     // moved

// --------- room options conversions ---------

LIVEKIT_INTERNAL_API proto::AudioEncoding toProto(const AudioEncodingOptions& in);
LIVEKIT_INTERNAL_API AudioEncodingOptions fromProto(const proto::AudioEncoding& in);

LIVEKIT_INTERNAL_API proto::VideoEncoding toProto(const VideoEncodingOptions& in);
LIVEKIT_INTERNAL_API VideoEncodingOptions fromProto(const proto::VideoEncoding& in);

LIVEKIT_INTERNAL_API proto::TrackPublishOptions toProto(const TrackPublishOptions& in);
LIVEKIT_INTERNAL_API TrackPublishOptions fromProto(const proto::TrackPublishOptions& in);

// --------- room Data Packet conversions ---------

LIVEKIT_INTERNAL_API UserDataPacketEvent userDataPacketFromProto(const proto::DataPacketReceived& in,
                                                                 RemoteParticipant* participant);

LIVEKIT_INTERNAL_API SipDtmfReceivedEvent sipDtmfFromProto(const proto::DataPacketReceived& in,
                                                           RemoteParticipant* participant);

// --------- room Data Stream conversions ---------
LIVEKIT_INTERNAL_API std::map<std::string, std::string> toAttrMap(const proto::DataStream::Trailer& trailer);
LIVEKIT_INTERNAL_API ByteStreamInfo makeByteInfo(const proto::DataStream::Header& header);
LIVEKIT_INTERNAL_API TextStreamInfo makeTextInfo(const proto::DataStream::Header& header);

} // namespace livekit
