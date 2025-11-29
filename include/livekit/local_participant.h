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

#include "livekit/ffi_handle.h"
#include "livekit/participant.h"
#include "livekit/room_delegate.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace livekit {

struct ParticipantTrackPermission;

class FfiClient;
class Track;
class LocalTrackPublication;
struct Transcription;

/**
 * Represents the local participant in a room.
 *
 * LocalParticipant, built on top of the participant.h base class.
 */
class LocalParticipant : public Participant {
public:
  using PublicationMap =
      std::unordered_map<std::string, std::shared_ptr<LocalTrackPublication>>;

  LocalParticipant(FfiHandle handle, std::string sid, std::string name,
                   std::string identity, std::string metadata,
                   std::unordered_map<std::string, std::string> attributes,
                   ParticipantKind kind, DisconnectReason reason);

  /// Track publications associated with this participant, keyed by track SID.
  const PublicationMap &trackPublications() const noexcept {
    return track_publications_;
  }

  /**
   * Publish arbitrary data to the room.
   *
   * @param payload                Raw bytes to send.
   * @param reliable               Whether to send reliably or not.
   * @param destination_identities Optional list of participant identities.
   * @param topic                  Optional topic string.
   *
   * Throws std::runtime_error if FFI reports an error (if you wire that up).
   */
  void publishData(const std::vector<std::uint8_t> &payload,
                   bool reliable = true,
                   const std::vector<std::string> &destination_identities = {},
                   const std::string &topic = {});

  /**
   * Publish SIP DTMF message.
   */
  void publishDtmf(int code, const std::string &digit);

  /**
   * Publish transcription data to the room.
   *
   * @param transcription
   */
  void publishTranscription(const Transcription &transcription);

  // -------------------------------------------------------------------------
  // Metadata APIs (set metadata / name / attributes)
  // -------------------------------------------------------------------------

  void setMetadata(const std::string &metadata);
  void setName(const std::string &name);
  void
  setAttributes(const std::unordered_map<std::string, std::string> &attributes);

  // -------------------------------------------------------------------------
  // Subscription permissions
  // -------------------------------------------------------------------------

  /**
   * Set track subscription permissions for this participant.
   *
   * @param allow_all_participants If true, all participants may subscribe.
   * @param participant_permissions Optional participant-specific permissions.
   */
  void
  setTrackSubscriptionPermissions(bool allow_all_participants,
                                  const std::vector<ParticipantTrackPermission>
                                      &participant_permissions = {});

  // -------------------------------------------------------------------------
  // Track publish / unpublish (synchronous analogue)
  // -------------------------------------------------------------------------

  /**
   * Publish a local track to the room.
   *
   * Throws std::runtime_error on error (e.g. publish failure).
   */
  std::shared_ptr<LocalTrackPublication>
  publishTrack(const std::shared_ptr<Track> &track,
               const TrackPublishOptions &options);

  /**
   * Unpublish a track from the room by SID.
   *
   * If the publication exists in the local map, it is removed.
   */
  void unpublishTrack(const std::string &track_sid);

private:
  PublicationMap track_publications_;
};

} // namespace livekit
