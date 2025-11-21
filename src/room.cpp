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

#include "livekit/room.h"

#include "livekit/ffi_client.h"
#include "livekit/local_participant.h"
#include "livekit/room_delegate.h"

#include "ffi.pb.h"
#include "room.pb.h"
#include "room_proto_converter.h"
#include "track_proto_converter.h"
#include <functional>
#include <iostream>

namespace livekit {

using proto::ConnectCallback;
using proto::ConnectRequest;
using proto::FfiEvent;
using proto::FfiRequest;
using proto::FfiResponse;
using proto::RoomOptions;

Room::Room() {}

Room::~Room() {}

void Room::setDelegate(RoomDelegate *delegate) {
  std::lock_guard<std::mutex> g(lock_);
  delegate_ = delegate;
}

bool Room::Connect(const std::string &url, const std::string &token) {
  auto listenerId = FfiClient::instance().AddListener(
      std::bind(&Room::OnEvent, this, std::placeholders::_1));
  {
    std::lock_guard<std::mutex> g(lock_);
    if (connected_) {
      FfiClient::instance().RemoveListener(listenerId);
      throw std::runtime_error("already connected");
    }
  }
  auto fut = FfiClient::instance().connectAsync(url, token);
  try {
    auto connectCb =
        fut.get(); // fut will throw if it fails to connect to the room
    {
      std::lock_guard<std::mutex> g(lock_);
      connected_ = true;
      const auto &ownedRoom = connectCb.result().room();
      room_handle_ = std::make_shared<FfiHandle>(ownedRoom.handle().id());
      room_info_ = fromProto(ownedRoom.info());
    }
    // Setup local particpant
    {
      const auto &owned_local = connectCb.result().local_participant();
      const auto &pinfo = owned_local.info();

      // Build attributes map
      std::unordered_map<std::string, std::string> attrs;
      for (const auto &kv : pinfo.attributes()) {
        attrs.emplace(kv.first, kv.second);
      }

      auto kind = fromProto(pinfo.kind());
      auto reason = toDisconnectReason(pinfo.disconnect_reason());

      // Participant base stores a weak_ptr<FfiHandle>, so share the room handle
      FfiHandle participant_handle(
          static_cast<uintptr_t>(owned_local.handle().id()));
      local_participant_ = std::make_unique<LocalParticipant>(
          std::move(participant_handle), pinfo.sid(), pinfo.name(),
          pinfo.identity(), pinfo.metadata(), std::move(attrs), kind, reason);
      std::cout << "creating local participant " << std::endl;
    }
    // Setup remote particpants
    {
      // TODO, implement this remote participant feature
    }
    return true;
  } catch (const std::exception &e) {
    // On error, remove the listener and rethrow
    FfiClient::instance().RemoveListener(listenerId);
    std::cerr << "Room::Connect failed: " << e.what() << std::endl;
    return false;
  }
}

RoomInfoData Room::room_info() const {
  std::lock_guard<std::mutex> g(lock_);
  return room_info_;
}

LocalParticipant *Room::local_participant() const {
  std::lock_guard<std::mutex> g(lock_);
  return local_participant_.get();
}

void Room::OnEvent(const FfiEvent &event) {
  // Take a snapshot of the delegate under lock, but do NOT call it under the
  // lock.
  RoomDelegate *delegate_snapshot = nullptr;

  {
    std::lock_guard<std::mutex> guard(lock_);
    delegate_snapshot = delegate_;
    // If you want, you can also update internal state here (participants, room
    // info, etc.).
  }

  if (!delegate_snapshot) {
    return;
  }

  switch (event.message_case()) {
  case FfiEvent::kRoomEvent: {
    const proto::RoomEvent &re = event.room_event();

    // Optional generic hook
    delegate_snapshot->onRoomEvent(*this);

    switch (re.message_case()) {
    case proto::RoomEvent::kParticipantConnected: {
      auto ev = fromProto(re.participant_connected());
      delegate_snapshot->onParticipantConnected(*this, ev);
      break;
    }
    case proto::RoomEvent::kParticipantDisconnected: {
      auto ev = fromProto(re.participant_disconnected());
      delegate_snapshot->onParticipantDisconnected(*this, ev);
      break;
    }
    case proto::RoomEvent::kLocalTrackPublished: {
      auto ev = fromProto(re.local_track_published());
      delegate_snapshot->onLocalTrackPublished(*this, ev);
      break;
    }
    case proto::RoomEvent::kLocalTrackUnpublished: {
      auto ev = fromProto(re.local_track_unpublished());
      delegate_snapshot->onLocalTrackUnpublished(*this, ev);
      break;
    }
    case proto::RoomEvent::kLocalTrackSubscribed: {
      auto ev = fromProto(re.local_track_subscribed());
      delegate_snapshot->onLocalTrackSubscribed(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackPublished: {
      auto ev = fromProto(re.track_published());
      delegate_snapshot->onTrackPublished(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackUnpublished: {
      auto ev = fromProto(re.track_unpublished());
      delegate_snapshot->onTrackUnpublished(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackSubscribed: {
      auto ev = fromProto(re.track_subscribed());
      delegate_snapshot->onTrackSubscribed(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackUnsubscribed: {
      auto ev = fromProto(re.track_unsubscribed());
      delegate_snapshot->onTrackUnsubscribed(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackSubscriptionFailed: {
      auto ev = fromProto(re.track_subscription_failed());
      delegate_snapshot->onTrackSubscriptionFailed(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackMuted: {
      auto ev = fromProto(re.track_muted());
      delegate_snapshot->onTrackMuted(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackUnmuted: {
      auto ev = fromProto(re.track_unmuted());
      delegate_snapshot->onTrackUnmuted(*this, ev);
      break;
    }
    case proto::RoomEvent::kActiveSpeakersChanged: {
      auto ev = fromProto(re.active_speakers_changed());
      delegate_snapshot->onActiveSpeakersChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kRoomMetadataChanged: {
      auto ev = fromProto(re.room_metadata_changed());
      delegate_snapshot->onRoomMetadataChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kRoomSidChanged: {
      auto ev = fromProto(re.room_sid_changed());
      delegate_snapshot->onRoomSidChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kParticipantMetadataChanged: {
      auto ev = fromProto(re.participant_metadata_changed());
      delegate_snapshot->onParticipantMetadataChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kParticipantNameChanged: {
      auto ev = fromProto(re.participant_name_changed());
      delegate_snapshot->onParticipantNameChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kParticipantAttributesChanged: {
      auto ev = fromProto(re.participant_attributes_changed());
      delegate_snapshot->onParticipantAttributesChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kParticipantEncryptionStatusChanged: {
      auto ev = fromProto(re.participant_encryption_status_changed());
      delegate_snapshot->onParticipantEncryptionStatusChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kConnectionQualityChanged: {
      auto ev = fromProto(re.connection_quality_changed());
      delegate_snapshot->onConnectionQualityChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kConnectionStateChanged: {
      auto ev = fromProto(re.connection_state_changed());
      delegate_snapshot->onConnectionStateChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kDisconnected: {
      auto ev = fromProto(re.disconnected());
      delegate_snapshot->onDisconnected(*this, ev);
      break;
    }
    case proto::RoomEvent::kReconnecting: {
      auto ev = fromProto(re.reconnecting());
      delegate_snapshot->onReconnecting(*this, ev);
      break;
    }
    case proto::RoomEvent::kReconnected: {
      auto ev = fromProto(re.reconnected());
      delegate_snapshot->onReconnected(*this, ev);
      break;
    }
    case proto::RoomEvent::kE2EeStateChanged: {
      auto ev = fromProto(re.e2ee_state_changed());
      delegate_snapshot->onE2eeStateChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kEos: {
      auto ev = fromProto(re.eos());
      delegate_snapshot->onRoomEos(*this, ev);
      break;
    }
    case proto::RoomEvent::kDataPacketReceived: {
      auto ev = fromProto(re.data_packet_received());
      delegate_snapshot->onDataPacketReceived(*this, ev);
      break;
    }
    case proto::RoomEvent::kTranscriptionReceived: {
      auto ev = fromProto(re.transcription_received());
      delegate_snapshot->onTranscriptionReceived(*this, ev);
      break;
    }
    case proto::RoomEvent::kChatMessage: {
      auto ev = fromProto(re.chat_message());
      delegate_snapshot->onChatMessageReceived(*this, ev);
      break;
    }
    case proto::RoomEvent::kStreamHeaderReceived: {
      auto ev = fromProto(re.stream_header_received());
      delegate_snapshot->onDataStreamHeaderReceived(*this, ev);
      break;
    }
    case proto::RoomEvent::kStreamChunkReceived: {
      auto ev = fromProto(re.stream_chunk_received());
      delegate_snapshot->onDataStreamChunkReceived(*this, ev);
      break;
    }
    case proto::RoomEvent::kStreamTrailerReceived: {
      auto ev = fromProto(re.stream_trailer_received());
      delegate_snapshot->onDataStreamTrailerReceived(*this, ev);
      break;
    }
    case proto::RoomEvent::kDataChannelLowThresholdChanged: {
      auto ev = fromProto(re.data_channel_low_threshold_changed());
      delegate_snapshot->onDataChannelBufferedAmountLowThresholdChanged(*this,
                                                                        ev);
      break;
    }
    case proto::RoomEvent::kByteStreamOpened: {
      auto ev = fromProto(re.byte_stream_opened());
      delegate_snapshot->onByteStreamOpened(*this, ev);
      break;
    }
    case proto::RoomEvent::kTextStreamOpened: {
      auto ev = fromProto(re.text_stream_opened());
      delegate_snapshot->onTextStreamOpened(*this, ev);
      break;
    }
    case proto::RoomEvent::kRoomUpdated: {
      auto ev = roomUpdatedFromProto(re.room_updated());
      delegate_snapshot->onRoomUpdated(*this, ev);
      break;
    }
    case proto::RoomEvent::kMoved: {
      auto ev = roomMovedFromProto(re.moved());
      delegate_snapshot->onRoomMoved(*this, ev);
      break;
    }
    case proto::RoomEvent::kParticipantsUpdated: {
      auto ev = fromProto(re.participants_updated());
      delegate_snapshot->onParticipantsUpdated(*this, ev);
      break;
    }

    case proto::RoomEvent::MESSAGE_NOT_SET:
    default:
      break;
    }

    break;
  }

  default:
    break;
  }
}

} // namespace livekit
