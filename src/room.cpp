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
#include "livekit/room_event_types.h"
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

std::shared_ptr<livekit::RemoteParticipant>
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
  return std::make_shared<livekit::RemoteParticipant>(
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
          rp->mutableTrackPublications().emplace(publication->sid(),
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
  }

  // First, handle RPC method invocations (not part of RoomEvent).
  if (event.message_case() == FfiEvent::kRpcMethodInvocation) {
    const auto &rpc = event.rpc_method_invocation();

    LocalParticipant *lp = nullptr;
    {
      std::lock_guard<std::mutex> guard(lock_);
      if (!local_participant_) {
        return;
      }
      auto local_handle = local_participant_->ffiHandleId();
      if (local_handle == INVALID_HANDLE ||
          rpc.local_participant_handle() !=
              static_cast<std::uint64_t>(local_handle)) {
        // RPC is not targeted at this room's local participant; ignore.
        return;
      }
      lp = local_participant_.get();
    }

    // Call outside the lock to avoid deadlocks / re-entrancy issues.
    lp->handleRpcMethodInvocation(
        rpc.invocation_id(), rpc.method(), rpc.request_id(),
        rpc.caller_identity(), rpc.payload(),
        static_cast<double>(rpc.response_timeout_ms()) / 1000.0);

    return;
  }

  if (!delegate_snapshot) {
    return;
  }

  switch (event.message_case()) {
  case FfiEvent::kRoomEvent: {
    const proto::RoomEvent &re = event.room_event();
    switch (re.message_case()) {
    case proto::RoomEvent::kParticipantConnected: {
      std::shared_ptr<RemoteParticipant> new_participant;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &owned = re.participant_connected().info();
        // createRemoteParticipant takes proto::OwnedParticipant
        new_participant = createRemoteParticipant(owned);
        remote_participants_.emplace(new_participant->identity(),
                                     new_participant);
      }
      ParticipantConnectedEvent ev;
      ev.participant = new_participant.get();
      delegate_snapshot->onParticipantConnected(*this, ev);

      break;
    }
    case proto::RoomEvent::kParticipantDisconnected: {
      std::shared_ptr<RemoteParticipant> removed;
      DisconnectReason reason = DisconnectReason::Unknown;

      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &pd = re.participant_disconnected();
        const std::string &identity = pd.participant_identity();
        reason = toDisconnectReason(pd.disconnect_reason());

        auto it = remote_participants_.find(identity);
        if (it != remote_participants_.end()) {
          removed = it->second;
          remote_participants_.erase(it);
        } else {
          // We saw a disconnect event for a participant we don't track
          // internally. This can happen on races or if we never created a
          // RemoteParticipant
          std::cerr << "participant_disconnected for unknown identity: "
                    << identity << std::endl;
        }
      }
      if (removed) {
        ParticipantDisconnectedEvent ev;
        ev.participant = removed.get();
        ev.reason = reason;
        delegate_snapshot->onParticipantDisconnected(*this, ev);
      }
      break;
    }
    case proto::RoomEvent::kLocalTrackPublished: {
      LocalTrackPublishedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        if (!local_participant_) {
          std::cerr << "kLocalTrackPublished: local_participant_ is nullptr"
                    << std::endl;
          break;
        }
        const auto &ltp = re.local_track_published();
        const std::string &sid = ltp.track_sid();
        auto &pubs = local_participant_->trackPublications();
        auto it = pubs.find(sid);
        if (it == pubs.end()) {
          std::cerr << "local_track_published for unknown sid: " << sid
                    << std::endl;
          break;
        }
        ev.publication = it->second;
        ev.track = ev.publication ? ev.publication->track() : nullptr;
      }
      delegate_snapshot->onLocalTrackPublished(*this, ev);
      break;
    }
    case proto::RoomEvent::kLocalTrackUnpublished: {
      LocalTrackUnpublishedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        if (!local_participant_) {
          std::cerr << "kLocalTrackPublished: local_participant_ is nullptr"
                    << std::endl;
          break;
        }
        const auto &ltu = re.local_track_unpublished();
        const std::string &pub_sid = ltu.publication_sid();
        auto &pubs = local_participant_->trackPublications();
        auto it = pubs.find(pub_sid);
        if (it == pubs.end()) {
          std::cerr << "local_track_unpublished for unknown publication sid: "
                    << pub_sid << std::endl;
          break;
        }
        ev.publication = it->second;
      }
      delegate_snapshot->onLocalTrackUnpublished(*this, ev);
      break;
    }
    case proto::RoomEvent::kLocalTrackSubscribed: {
      LocalTrackSubscribedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        if (!local_participant_) {
          break;
        }
        const auto &lts = re.local_track_subscribed();
        const std::string &sid = lts.track_sid();
        auto &pubs = local_participant_->trackPublications();
        auto it = pubs.find(sid);
        if (it == pubs.end()) {
          std::cerr << "local_track_subscribed for unknown sid: " << sid
                    << std::endl;
          break;
        }
        auto publication = it->second;
        ev.track = publication ? publication->track() : nullptr;
      }

      delegate_snapshot->onLocalTrackSubscribed(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackPublished: {
      TrackPublishedEvent ev;
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
          rparticipant->mutableTrackPublications().emplace(
              rpublication->sid(), std::move(rpublication));
          ev.participant = rparticipant;
          ev.publication = rpublication;
        } else {
          // Optional: log if we get a track for an unknown participant
          std::cerr << "track_published for unknown participant: " << identity
                    << std::endl;
          // Don't emit the
          break;
        }
      }
      delegate_snapshot->onTrackPublished(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackUnpublished: {
      TrackUnpublishedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &tu = re.track_unpublished();
        const std::string &identity = tu.participant_identity();
        const std::string &pub_sid = tu.publication_sid();
        auto pit = remote_participants_.find(identity);
        if (pit == remote_participants_.end()) {
          std::cerr << "track_unpublished for unknown participant: " << identity
                    << std::endl;
          break;
        }
        RemoteParticipant *rparticipant = pit->second.get();
        auto &pubs = rparticipant->mutableTrackPublications();
        auto it = pubs.find(pub_sid);
        if (it == pubs.end()) {
          std::cerr << "track_unpublished for unknown publication sid "
                    << pub_sid << " (participant " << identity << ")\n";
          break;
        }
        ev.participant = rparticipant;
        ev.publication = it->second;
        pubs.erase(it);
      }

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
        auto &pubs = rparticipant->mutableTrackPublications();
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
        // Attach to publication, mark subscribed
        rpublication->setTrack(remote_track);
        rpublication->setSubscribed(true);
      }

      // Emit remote track_subscribed-style callback
      TrackSubscribedEvent ev;
      ev.track = remote_track;
      ev.publication = rpublication;
      ev.participant = rparticipant;
      delegate_snapshot->onTrackSubscribed(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackUnsubscribed: {
      TrackUnsubscribedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &tu = re.track_unsubscribed();
        const std::string &identity = tu.participant_identity();
        const std::string &track_sid = tu.track_sid();
        auto pit = remote_participants_.find(identity);
        if (pit == remote_participants_.end()) {
          std::cerr << "track_unsubscribed for unknown participant: "
                    << identity << "\n";
          break;
        }
        RemoteParticipant *rparticipant = pit->second.get();
        auto &pubs = rparticipant->mutableTrackPublications();
        auto pubIt = pubs.find(track_sid);
        if (pubIt == pubs.end()) {
          std::cerr << "track_unsubscribed for unknown publication sid "
                    << track_sid << " (participant " << identity << ")\n";
          break;
        }
        auto publication = pubIt->second;
        auto track = publication->track();
        publication->setTrack(nullptr);
        publication->setSubscribed(false);
        ev.participant = rparticipant;
        ev.publication = publication;
        ev.track = track;
      }

      delegate_snapshot->onTrackUnsubscribed(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackSubscriptionFailed: {
      TrackSubscriptionFailedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &tsf = re.track_subscription_failed();
        const std::string &identity = tsf.participant_identity();
        auto pit = remote_participants_.find(identity);
        if (pit == remote_participants_.end()) {
          std::cerr << "track_subscription_failed for unknown participant: "
                    << identity << "\n";
          break;
        }
        ev.participant = pit->second.get();
        ev.track_sid = tsf.track_sid();
        ev.error = tsf.error();
      }
      delegate_snapshot->onTrackSubscriptionFailed(*this, ev);
      break;
    }
    case proto::RoomEvent::kTrackMuted: {
      TrackMutedEvent ev;
      bool success = false;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &tm = re.track_muted();
        const std::string &identity = tm.participant_identity();
        const std::string &sid = tm.track_sid();
        Participant *participant = nullptr;
        if (local_participant_ && local_participant_->identity() == identity) {
          participant = local_participant_.get();
        } else {
          auto pit = remote_participants_.find(identity);
          if (pit != remote_participants_.end()) {
            participant = pit->second.get();
          }
        }
        if (!participant) {
          std::cerr << "track_muted for unknown participant: " << identity
                    << "\n";
          break;
        }
        auto pub = participant->findTrackPublication(sid);
        if (!pub) {
          std::cerr << "track_muted for unknown track sid: " << sid
                    << std::endl;
        } else {
          pub->setMuted(true);
          if (auto t = pub->track()) {
            t->setMuted(true);
          }
          ev.participant = participant;
          ev.publication = pub;
          success = true;
        }
      }
      if (success) {
        delegate_snapshot->onTrackMuted(*this, ev);
      }
      break;
    }
    case proto::RoomEvent::kTrackUnmuted: {
      TrackUnmutedEvent ev;
      bool success = false;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &tu = re.track_unmuted();
        const std::string &identity = tu.participant_identity();
        const std::string &sid = tu.track_sid();
        Participant *participant = nullptr;
        if (local_participant_ && local_participant_->identity() == identity) {
          participant = local_participant_.get();
        } else {
          auto pit = remote_participants_.find(identity);
          if (pit != remote_participants_.end()) {
            participant = pit->second.get();
          }
        }
        if (!participant) {
          std::cerr << "track_unmuted for unknown participant: " << identity
                    << "\n";
          break;
        }

        auto pub = participant->findTrackPublication(sid);
        if (!pub) {
          std::cerr << "track_muted for unknown track sid: " << sid
                    << std::endl;
        } else {
          pub->setMuted(false);
          if (auto t = pub->track()) {
            t->setMuted(false);
          }
          ev.participant = participant;
          ev.publication = pub;
          success = true;
        }

        ev.participant = participant;
        ev.publication = pub;
      }

      if (success) {
        delegate_snapshot->onTrackUnmuted(*this, ev);
      }
      break;
    }
    case proto::RoomEvent::kActiveSpeakersChanged: {
      ActiveSpeakersChangedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &asc = re.active_speakers_changed();
        for (const auto &identity : asc.participant_identities()) {
          Participant *participant = nullptr;
          if (local_participant_ &&
              local_participant_->identity() == identity) {
            participant = local_participant_.get();
          } else {
            auto pit = remote_participants_.find(identity);
            if (pit != remote_participants_.end()) {
              participant = pit->second.get();
            }
          }
          if (participant) {
            ev.speakers.push_back(participant);
          }
        }
      }
      delegate_snapshot->onActiveSpeakersChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kRoomMetadataChanged: {
      RoomMetadataChangedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto old_metadata = room_info_.metadata;
        room_info_.metadata = re.room_metadata_changed().metadata();
        ev.old_metadata = old_metadata;
        ev.new_metadata = room_info_.metadata;
      }
      delegate_snapshot->onRoomMetadataChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kRoomSidChanged: {
      RoomSidChangedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        room_info_.sid = re.room_sid_changed().sid();
        ev.sid = room_info_.sid.value_or(std::string{});
      }
      delegate_snapshot->onRoomSidChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kParticipantMetadataChanged: {
      ParticipantMetadataChangedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &pm = re.participant_metadata_changed();
        const std::string &identity = pm.participant_identity();
        Participant *participant = nullptr;
        if (local_participant_ && local_participant_->identity() == identity) {
          participant = local_participant_.get();
        } else {
          auto it = remote_participants_.find(identity);
          if (it != remote_participants_.end()) {
            participant = it->second.get();
          }
        }
        if (!participant) {
          std::cerr << "participant_metadata_changed for unknown participant: "
                    << identity << "\n";
          break;
        }
        std::string old_metadata = participant->metadata();
        participant->set_metadata(pm.metadata());
        ev.participant = participant;
        ev.old_metadata = old_metadata;
        ev.new_metadata = participant->metadata();
      }

      delegate_snapshot->onParticipantMetadataChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kParticipantNameChanged: {
      ParticipantNameChangedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &pn = re.participant_name_changed();
        const std::string &identity = pn.participant_identity();
        Participant *participant = nullptr;
        if (local_participant_ && local_participant_->identity() == identity) {
          participant = local_participant_.get();
        } else {
          auto it = remote_participants_.find(identity);
          if (it != remote_participants_.end()) {
            participant = it->second.get();
          }
        }
        if (!participant) {
          std::cerr << "participant_name_changed for unknown participant: "
                    << identity << "\n";
          break;
        }
        std::string old_name = participant->name();
        participant->set_name(pn.name());
        ev.participant = participant;
        ev.old_name = old_name;
        ev.new_name = participant->name();
      }
      delegate_snapshot->onParticipantNameChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kParticipantAttributesChanged: {
      ParticipantAttributesChangedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &pa = re.participant_attributes_changed();
        const std::string &identity = pa.participant_identity();
        Participant *participant = nullptr;
        if (local_participant_ && local_participant_->identity() == identity) {
          participant = local_participant_.get();
        } else {
          auto it = remote_participants_.find(identity);
          if (it != remote_participants_.end()) {
            participant = it->second.get();
          }
        }
        if (!participant) {
          std::cerr
              << "participant_attributes_changed for unknown participant: "
              << identity << "\n";
          break;
        }
        // Build full attributes map
        std::unordered_map<std::string, std::string> attrs;
        for (const auto &entry : pa.attributes()) {
          attrs.emplace(entry.key(), entry.value());
        }
        participant->set_attributes(attrs);

        // Build changed_attributes map
        for (const auto &entry : pa.changed_attributes()) {
          ev.changed_attributes.emplace_back(entry.key(), entry.value());
        }
        ev.participant = participant;
      }
      delegate_snapshot->onParticipantAttributesChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kParticipantEncryptionStatusChanged: {
      ParticipantEncryptionStatusChangedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &pe = re.participant_encryption_status_changed();
        const std::string &identity = pe.participant_identity();
        Participant *participant = nullptr;
        if (local_participant_ && local_participant_->identity() == identity) {
          participant = local_participant_.get();
        } else {
          auto it = remote_participants_.find(identity);
          if (it != remote_participants_.end()) {
            participant = it->second.get();
          }
        }
        if (!participant) {
          std::cerr << "participant_encryption_status_changed for unknown "
                       "participant: "
                    << identity << "\n";
          break;
        }
        ev.participant = participant;
        ev.is_encrypted = pe.is_encrypted();
      }

      delegate_snapshot->onParticipantEncryptionStatusChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kConnectionQualityChanged: {
      ConnectionQualityChangedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &cq = re.connection_quality_changed();
        const std::string &identity = cq.participant_identity();
        Participant *participant = nullptr;
        if (local_participant_ && local_participant_->identity() == identity) {
          participant = local_participant_.get();
        } else {
          auto it = remote_participants_.find(identity);
          if (it != remote_participants_.end()) {
            participant = it->second.get();
          }
        }
        if (!participant) {
          std::cerr << "connection_quality_changed for unknown participant: "
                    << identity << "\n";
          break;
        }
        ev.participant = participant;
        ev.quality = static_cast<ConnectionQuality>(cq.quality());
      }

      delegate_snapshot->onConnectionQualityChanged(*this, ev);
      break;
    }

      // ------------------------------------------------------------------------
      // Transcription
      // ------------------------------------------------------------------------

    case proto::RoomEvent::kTranscriptionReceived: {
      TranscriptionReceivedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &tr = re.transcription_received();
        for (const auto &s : tr.segments()) {
          TranscriptionSegment seg;
          seg.id = s.id();
          seg.text = s.text();
          seg.final = s.final();
          seg.start_time = s.start_time();
          seg.end_time = s.end_time();
          seg.language = s.language();
          ev.segments.push_back(std::move(seg));
        }

        Participant *participant = nullptr;
        if (!tr.participant_identity().empty()) {
          const std::string &identity = tr.participant_identity();
          if (local_participant_ &&
              local_participant_->identity() == identity) {
            participant = local_participant_.get();
          } else {
            auto it = remote_participants_.find(identity);
            if (it != remote_participants_.end()) {
              participant = it->second.get();
            }
          }
        }
        ev.participant = participant;
        ev.publication = participant->findTrackPublication(tr.track_sid());
      }

      delegate_snapshot->onTranscriptionReceived(*this, ev);
      break;
    }

    // ------------------------------------------------------------------------
    // Data packets: user vs SIP DTMF
    // ------------------------------------------------------------------------
    case proto::RoomEvent::kDataPacketReceived: {
      const auto &dp = re.data_packet_received();
      RemoteParticipant *rp = nullptr;
      {
        std::lock_guard<std::mutex> guard(lock_);
        auto it = remote_participants_.find(dp.participant_identity());
        if (it != remote_participants_.end()) {
          rp = it->second.get();
        }
      }
      const auto which_val = dp.value_case();
      if (which_val == proto::DataPacketReceived::kUser) {
        UserDataPacketEvent ev = userDataPacketFromProto(dp, rp);
        delegate_snapshot->onUserPacketReceived(*this, ev);
      } else if (which_val == proto::DataPacketReceived::kSipDtmf) {
        SipDtmfReceivedEvent ev = sipDtmfFromProto(dp, rp);
        delegate_snapshot->onSipDtmfReceived(*this, ev);
      }
      break;
    }

    // ------------------------------------------------------------------------
    // E2EE state
    // ------------------------------------------------------------------------
    case proto::RoomEvent::kE2EeStateChanged: {
      E2eeStateChangedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &es = re.e2ee_state_changed();
        const std::string &identity = es.participant_identity();
        Participant *participant = nullptr;
        if (local_participant_ && local_participant_->identity() == identity) {
          participant = local_participant_.get();
        } else {
          auto it = remote_participants_.find(identity);
          if (it != remote_participants_.end()) {
            participant = it->second.get();
          }
        }
        if (!participant) {
          std::cerr << "e2ee_state_changed for unknown participant: "
                    << identity << std::endl;
          break;
        }

        ev.participant = participant;
        ev.state = static_cast<EncryptionState>(es.state());
      }
      delegate_snapshot->onE2eeStateChanged(*this, ev);
      break;
    }

      // ------------------------------------------------------------------------
      // Connection state / lifecycle
      // ------------------------------------------------------------------------

    case proto::RoomEvent::kConnectionStateChanged: {
      ConnectionStateChangedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &cs = re.connection_state_changed();
        connection_state_ = static_cast<ConnectionState>(cs.state());
        ev.state = connection_state_;
      }
      delegate_snapshot->onConnectionStateChanged(*this, ev);
      break;
    }
    case proto::RoomEvent::kDisconnected: {
      DisconnectedEvent ev;
      ev.reason = toDisconnectReason(re.disconnected().reason());
      delegate_snapshot->onDisconnected(*this, ev);
      break;
    }
    case proto::RoomEvent::kReconnecting: {
      ReconnectingEvent ev;
      delegate_snapshot->onReconnecting(*this, ev);
      break;
    }
    case proto::RoomEvent::kReconnected: {
      ReconnectedEvent ev;
      delegate_snapshot->onReconnected(*this, ev);
      break;
    }
    case proto::RoomEvent::kEos: {
      RoomEosEvent ev;
      delegate_snapshot->onRoomEos(*this, ev);
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
      ParticipantsUpdatedEvent ev;
      {
        std::lock_guard<std::mutex> guard(lock_);
        const auto &pu = re.participants_updated();
        for (const auto &info : pu.participants()) {
          const std::string &identity = info.identity();
          Participant *participant = nullptr;

          if (local_participant_ &&
              identity == local_participant_->identity()) {
            participant = local_participant_.get();
          } else {
            auto it = remote_participants_.find(identity);
            if (it != remote_participants_.end()) {
              participant = it->second.get();
            }
          }
          if (!participant) {
            std::cerr << "Room::RoomEvent::kParticipantsUpdated participant "
                         "does not exist: "
                      << identity << std::endl;
            continue;
          }

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

          ev.participants.push_back(participant);
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
