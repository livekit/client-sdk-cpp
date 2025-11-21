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

#include "livekit/local_participant.h"

#include "livekit/ffi_client.h"
#include "livekit/ffi_handle.h"
#include "livekit/local_track_publication.h"
#include "livekit/room_delegate.h"
#include "livekit/track.h"

#include "ffi.pb.h"
#include "participant.pb.h"
#include "room.pb.h"
#include "room_proto_converter.h"
#include "track.pb.h"
#include "track_proto_converter.h"

#include <stdexcept>

namespace livekit {

using proto::FfiRequest;
using proto::FfiResponse;

LocalParticipant::LocalParticipant(
    FfiHandle handle, std::string sid, std::string name, std::string identity,
    std::string metadata,
    std::unordered_map<std::string, std::string> attributes,
    ParticipantKind kind, DisconnectReason reason)
    : Participant(std::move(handle), std::move(sid), std::move(name),
                  std::move(identity), std::move(metadata),
                  std::move(attributes), kind, reason) {}

void LocalParticipant::publishData(
    const std::vector<std::uint8_t> &payload, bool reliable,
    const std::vector<std::string> &destination_identities,
    const std::string &topic) {
  if (payload.empty()) {
    return;
  }

  auto handle_id = ffiHandleId();
  if (handle_id == 0) {
    throw std::runtime_error(
        "LocalParticipant::publishData: invalid FFI handle");
  }

  // Use async FFI API and block until completion.
  auto fut = FfiClient::instance().publishDataAsync(
      static_cast<std::uint64_t>(handle_id), payload.data(),
      static_cast<std::uint64_t>(payload.size()), reliable,
      destination_identities, topic);

  fut.get();
}

void LocalParticipant::publishDtmf(int code, const std::string &digit) {
  auto handle_id = ffiHandleId();
  if (handle_id == 0) {
    throw std::runtime_error(
        "LocalParticipant::publishDtmf: invalid FFI handle");
  }

  // TODO, should we take destination as inputs?
  std::vector<std::string> destination_identities;
  auto fut = FfiClient::instance().publishSipDtmfAsync(
      static_cast<std::uint64_t>(handle_id), static_cast<std::uint32_t>(code),
      digit, destination_identities);

  fut.get();
}

void LocalParticipant::publishTranscription(
    const Transcription &transcription) {
  auto handle_id = ffiHandleId();
  if (handle_id == 0) {
    throw std::runtime_error(
        "LocalParticipant::publishTranscription: invalid FFI handle");
  }

  std::vector<proto::TranscriptionSegment> segs;
  segs.reserve(transcription.segments.size());
  for (const auto &seg : transcription.segments) {
    segs.push_back(toProto(seg));
  }

  // Handle optional participant_identity / track_sid
  const std::string participant_identity =
      transcription.participant_identity.has_value()
          ? *transcription.participant_identity
          : std::string{};
  const std::string track_sid = transcription.track_sid.has_value()
                                    ? *transcription.track_sid
                                    : std::string{};
  // Call async API and block until completion
  auto fut = FfiClient::instance().publishTranscriptionAsync(
      static_cast<std::uint64_t>(handle_id), participant_identity, track_sid,
      segs);
  fut.get();
}

void LocalParticipant::setMetadata(const std::string &metadata) {
  auto handle_id = ffiHandleId();
  if (handle_id == 0) {
    throw std::runtime_error(
        "LocalParticipant::setMetadata: invalid FFI handle");
  }
  auto fut = FfiClient::instance().setLocalMetadataAsync(
      static_cast<std::uint64_t>(handle_id), metadata);

  fut.get();
}

void LocalParticipant::setName(const std::string &name) {
  auto handle_id = ffiHandleId();
  if (handle_id == 0) {
    throw std::runtime_error("LocalParticipant::setName: invalid FFI handle");
  }

  // No async helper defined for SetLocalName in FfiClient yet, so keep using
  // the direct request.
  FfiRequest req;
  auto *msg = req.mutable_set_local_name();
  msg->set_local_participant_handle(static_cast<std::uint64_t>(handle_id));
  msg->set_name(name);

  (void)FfiClient::instance().sendRequest(req);
}

void LocalParticipant::setAttributes(
    const std::unordered_map<std::string, std::string> &attributes) {
  auto handle_id = ffiHandleId();
  if (handle_id == 0) {
    throw std::runtime_error(
        "LocalParticipant::setAttributes: invalid FFI handle");
  }

  // No async helper defined for SetLocalAttributes in FfiClient yet.
  FfiRequest req;
  auto *msg = req.mutable_set_local_attributes();
  msg->set_local_participant_handle(static_cast<std::uint64_t>(handle_id));

  for (const auto &kv : attributes) {
    auto *entry = msg->add_attributes();
    entry->set_key(kv.first);
    entry->set_value(kv.second);
  }

  (void)FfiClient::instance().sendRequest(req);
}

// ----------------------------------------------------------------------------
// Subscription permissions
// ----------------------------------------------------------------------------

void LocalParticipant::setTrackSubscriptionPermissions(
    bool allow_all_participants,
    const std::vector<ParticipantTrackPermission> &participant_permissions) {
  auto handle_id = ffiHandleId();
  if (handle_id == 0) {
    throw std::runtime_error(
        "LocalParticipant::setTrackSubscriptionPermissions: invalid FFI "
        "handle");
  }

  // No dedicated async helper; do it directly.
  FfiRequest req;
  auto *msg = req.mutable_set_track_subscription_permissions();
  msg->set_local_participant_handle(static_cast<std::uint64_t>(handle_id));
  msg->set_all_participants_allowed(allow_all_participants);

  for (const auto &perm : participant_permissions) {
    auto *p = msg->add_permissions();
    p->CopyFrom(toProto(perm));
  }

  (void)FfiClient::instance().sendRequest(req);
}

// ----------------------------------------------------------------------------
// Track publish / unpublish
// ----------------------------------------------------------------------------

std::shared_ptr<LocalTrackPublication>
LocalParticipant::publishTrack(const std::shared_ptr<Track> &track,
                               const TrackPublishOptions &options) {
  if (!track) {
    throw std::invalid_argument(
        "LocalParticipant::publishTrack: track is null");
  }

  auto participant_handle = ffiHandleId();
  std::cout << "participant_handle is " << participant_handle << std::endl;
  if (participant_handle == 0) {
    throw std::runtime_error(
        "LocalParticipant::publishTrack: invalid participant FFI handle");
  }

  auto track_handle = track->ffi_handle_id();
  std::cout << "track_handle is " << track_handle << std::endl;

  if (track_handle == 0) {
    throw std::runtime_error(
        "LocalParticipant::publishTrack: invalid track FFI handle");
  }
  std::cout << "calling  publishTrackAsync" << std::endl;
  auto fut = FfiClient::instance().publishTrackAsync(
      static_cast<std::uint64_t>(participant_handle),
      static_cast<std::uint64_t>(track_handle), options);

  // Will throw if the async op fails (error in callback).
  proto::OwnedTrackPublication owned_pub = fut.get();

  // Construct a LocalTrackPublication from the proto publication.
  auto publication = std::make_shared<LocalTrackPublication>(owned_pub);

  // Cache in local map by track SID.
  const std::string sid = publication->sid();
  track_publications_[sid] = publication;

  return publication;
}

void LocalParticipant::unpublishTrack(const std::string &track_sid) {
  if (track_sid.empty()) {
    return;
  }

  auto handle_id = ffiHandleId();
  if (handle_id == 0) {
    throw std::runtime_error(
        "LocalParticipant::unpublishTrack: invalid FFI handle");
  }

  auto fut = FfiClient::instance().unpublishTrackAsync(
      static_cast<std::uint64_t>(handle_id), track_sid,
      /*stop_on_unpublish=*/true);

  fut.get();

  track_publications_.erase(track_sid);
}

} // namespace livekit
