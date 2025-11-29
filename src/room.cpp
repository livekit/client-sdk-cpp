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

#include "livekit/room.h"

#include "livekit/audio_stream.h"
#include "livekit/ffi_client.h"
#include "livekit/local_participant.h"
#include "livekit/local_track_publication.h"
#include "livekit/remote_audio_track.h"
#include "livekit/remote_participant.h"
#include "livekit/remote_track_publication.h"
#include "livekit/remote_video_track.h"
#include "livekit/room_delegate.h"
#include "livekit/video_stream.h"

#include "ffi.pb.h"
#include "room.pb.h"
#include "room_proto_converter.h"
#include "track.pb.h"
#include "track_proto_converter.h"
#include <functional>
#include <iostream>

namespace livekit {

using proto::ConnectCallback;
using proto::ConnectRequest;
using proto::FfiEvent;
using proto::FfiRequest;
using proto::FfiResponse;

namespace {

std::unique_ptr<livekit::RemoteParticipant>
createRemoteParticipant(const proto::OwnedParticipant &owned) {
  const auto &pinfo = owned.info();
  std::unordered_map<std::string, std::string> attrs;
  attrs.reserve(pinfo.attributes_size());
  for (const auto &kv : pinfo.attributes()) {
    attrs.emplace(kv.first, kv.second);
  }
  auto kind = livekit::fromProto(pinfo.kind());
  auto reason = livekit::toDisconnectReason(pinfo.disconnect_reason());
  livekit::FfiHandle handle(static_cast<uintptr_t>(owned.handle().id()));
  return std::make_unique<livekit::RemoteParticipant>(
      std::move(handle), pinfo.sid(), pinfo.name(), pinfo.identity(),
      pinfo.metadata(), std::move(attrs), kind, reason);
}

} // namespace
Room::Room() {}

Room::~Room() {}

void Room::setDelegate(RoomDelegate *delegate) {
  std::lock_guard<std::mutex> g(lock_);
  delegate_ = delegate;
}

bool Room::Connect(const std::string &url, const std::string &token,
                   const RoomOptions &options) {
  auto listenerId = FfiClient::instance().AddListener(
      std::bind(&Room::OnEvent, this, std::placeholders::_1));
  {
    std::lock_guard<std::mutex> g(lock_);
    if (connected_) {
      FfiClient::instance().RemoveListener(listenerId);
      throw std::runtime_error("already connected");
    }
  }
  auto fut = FfiClient::instance().connectAsync(url, token, options);
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
    }
    // Setup remote participants
    {
      const auto &participants = connectCb.result().participants();
      std::lock_guard<std::mutex> g(lock_);
      for (const auto &pt : participants) {
        const auto &owned = pt.participant();
        auto rp = createRemoteParticipant(owned);
        // Add the initial remote participant tracks (like Python does)
        for (const auto &owned_publication_info : pt.publications()) {
          auto publication =
              std::make_shared<RemoteTrackPublication>(owned_publication_info);
          rp->mutable_track_publications().emplace(publication->sid(),
                                                   std::move(publication));
        }

        remote_participants_.emplace(rp->identity(), std::move(rp));
      }
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

RemoteParticipant *Room::remote_participant(const std::string &identity) const {
  std::lock_guard<std::mutex> g(lock_);
  auto it = remote_participants_.find(identity);
  return it == remote_participants_.end() ? nullptr : it->second.get();
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
      std::cout << "kParticipantConnected " << std::endl;
      // Create and register RemoteParticipant
      {
        std::lock_guard<std::mutex> guard(lock_);
        auto rp = createRemoteParticipant(re.participant_connected().info());
        remote_participants_.emplace(rp->identity(), std::move(rp));
      }
      // TODO, use better public callback events
      delegate_snapshot->onParticipantConnected(*this, ev);

      break;
    }
    case proto::RoomEvent::kParticipantDisconnected: {
      auto ev = fromProto(re.participant_disconnected());
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &pd = re.participant_disconnected();
        const std::string &identity = pd.participant_identity();
        auto it = remote_participants_.find(identity);
        if (it != remote_participants_.end()) {
          remote_participants_.erase(it);
        } else {
          // We saw a disconnect event for a participant we don't track
          // internally. This can happen on races or if we never created a
          // RemoteParticipant
          std::cerr << "participant_disconnected for unknown identity: "
                    << identity << std::endl;
        }
      }
      // TODO, should we trigger onParticipantDisconnected if remote
      // participants can't be found ?
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
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &tp = re.track_published();
        const std::string &identity = tp.participant_identity();
        auto it = remote_participants_.find(identity);
        if (it != remote_participants_.end()) {
          RemoteParticipant *rparticipant = it->second.get();
          const auto &owned_publication = tp.publication();
          auto rpublication =
              std::make_shared<RemoteTrackPublication>(owned_publication);
          // Store it on the participant, keyed by SID
          rparticipant->mutable_track_publications().emplace(
              rpublication->sid(), std::move(rpublication));

        } else {
          // Optional: log if we get a track for an unknown participant
          std::cerr << "track_published for unknown participant: " << identity
                    << "\n";
          // Don't emit the
          break;
        }
      }
      delegate_snapshot->onTrackPublished(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackUnpublished: {
      auto ev = fromProto(re.track_unpublished());
      delegate_snapshot->onTrackUnpublished(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackSubscribed: {
      const auto &ts = re.track_subscribed();
      const std::string &identity = ts.participant_identity();
      const auto &owned_track = ts.track();
      const auto &track_info = owned_track.info();
      std::shared_ptr<RemoteTrackPublication> rpublication;
      RemoteParticipant *rparticipant = nullptr;
      std::shared_ptr<Track> remote_track;
      {
        std::lock_guard<std::mutex> guard(lock_);
        // Find participant
        auto pit = remote_participants_.find(identity);
        if (pit == remote_participants_.end()) {
          std::cerr << "track_subscribed for unknown participant: " << identity
                    << "\n";
          break;
        }
        rparticipant = pit->second.get();
        // Find existing publication by track SID (from track_published)
        auto &pubs = rparticipant->mutable_track_publications();
        auto pubIt = pubs.find(track_info.sid());
        if (pubIt == pubs.end()) {
          std::cerr << "track_subscribed for unknown publication sid "
                    << track_info.sid() << " (participant " << identity
                    << ")\n";
          break;
        }
        rpublication = pubIt->second;

        // Create RemoteVideoTrack / RemoteAudioTrack
        if (track_info.kind() == proto::TrackKind::KIND_VIDEO) {
          remote_track = std::make_shared<RemoteVideoTrack>(owned_track);
        } else if (track_info.kind() == proto::TrackKind::KIND_AUDIO) {
          remote_track = std::make_shared<RemoteAudioTrack>(owned_track);
        } else {
          std::cerr << "track_subscribed with unsupported kind: "
                    << track_info.kind() << "\n";
          break;
        }
        std::cout << "before setTrack " << std::endl;

        // Attach to publication, mark subscribed
        rpublication->setTrack(remote_track);
        std::cout << "setTrack " << std::endl;
        rpublication->setSubscribed(true);
        std::cout << "setSubscribed " << std::endl;
      }

      // Emit remote track_subscribed-style callback
      TrackSubscribedEvent ev;
      ev.track = remote_track;
      ev.publication = rpublication;
      ev.participant = rparticipant;
      std::cout << "onTrackSubscribed " << std::endl;
      delegate_snapshot->onTrackSubscribed(*this, ev);
      std::cout << "after onTrackSubscribed " << std::endl;
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
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &pu = re.participants_updated();
        for (const auto &info : pu.participants()) {
          const std::string &identity = info.identity();
          Participant *participant = nullptr;
          // First, check local participant.
          if (local_participant_ &&
              identity == local_participant_->identity()) {
            participant = local_participant_.get();
          } else {
            // Otherwise, look for a remote participant.
            auto it = remote_participants_.find(identity);
            if (it != remote_participants_.end()) {
              participant = it->second.get();
            }
          }

          if (!participant) {
            // Participant might not exist yet; ignore for now.
            std::cerr << "Room::RoomEvent::kParticipantsUpdated participant "
                         "does not exist: "
                      << identity << std::endl;
            continue;
          }

          // Update basic fields
          participant->set_name(info.name());
          participant->set_metadata(info.metadata());
          std::unordered_map<std::string, std::string> attrs;
          attrs.reserve(info.attributes_size());
          for (const auto &kv : info.attributes()) {
            attrs.emplace(kv.first, kv.second);
          }
          participant->set_attributes(std::move(attrs));
          participant->set_kind(fromProto(info.kind()));
          participant->set_disconnect_reason(
              toDisconnectReason(info.disconnect_reason()));
        }
      }
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
