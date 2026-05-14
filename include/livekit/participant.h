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

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "livekit/ffi_handle.h"
#include "livekit/room_delegate.h"
#include "livekit/visibility.h"

namespace livekit {

enum class ParticipantKind { Standard = 0, Ingress, Egress, Sip, Agent };

class LIVEKIT_API Participant {
public:
  Participant(FfiHandle handle, std::string sid, std::string name, std::string identity, std::string metadata,
              std::unordered_map<std::string, std::string> attributes, ParticipantKind kind, DisconnectReason reason)
      : handle_(std::move(handle)),
        sid_(std::move(sid)),
        name_(std::move(name)),
        identity_(std::move(identity)),
        metadata_(std::move(metadata)),
        attributes_(std::move(attributes)),
        kind_(kind),
        reason_(reason) {}
  virtual ~Participant() = default;

  // Plain getters (caller ensures threading)
  const std::string& sid() const noexcept { return sid_; }
  const std::string& name() const noexcept { return name_; }
  const std::string& identity() const noexcept { return identity_; }
  const std::string& metadata() const noexcept { return metadata_; }
  const std::unordered_map<std::string, std::string>& attributes() const noexcept { return attributes_; }
  ParticipantKind kind() const noexcept { return kind_; }
  DisconnectReason disconnectReason() const noexcept { return reason_; }

  uintptr_t ffiHandleId() const noexcept { return handle_.get(); }

  // ---------------------------------------------------------------------------
  // Deprecated public mutators
  //
  // These setters only mutate the local in-memory cache; they do *not*
  // synchronize with the LiveKit server. They were originally exposed as
  // public for the benefit of internal SDK plumbing (the Room class mirroring
  // server-pushed state), but this is not a sensible operation for SDK
  // consumers. They will be removed from the public API in a future major
  // release.
  //
  // To update name / metadata / attributes on the server, use
  // LocalParticipant::setName / setMetadata / setAttributes, which dispatch
  // an FFI request and propagate the change to other participants. Kind and
  // DisconnectReason are server-determined and are not user-settable.
  // ---------------------------------------------------------------------------

  [[deprecated(
      "Participant::setName is an internal cache mutator (does not sync with the server) and will be "
      "removed from the public API; use LocalParticipant::setName for server-synced updates")]]
  void setName(std::string name) noexcept {
    setNameInternal(std::move(name));
  }

  [[deprecated(
      "Participant::setMetadata is an internal cache mutator (does not sync with the server) and will be "
      "removed from the public API; use LocalParticipant::setMetadata for server-synced updates")]]
  void setMetadata(std::string metadata) noexcept {
    setMetadataInternal(std::move(metadata));
  }

  [[deprecated(
      "Participant::setAttributes is an internal cache mutator (does not sync with the server) and will be "
      "removed from the public API; use LocalParticipant::setAttributes for server-synced updates")]]
  void setAttributes(std::unordered_map<std::string, std::string> attrs) noexcept {
    setAttributesInternal(std::move(attrs));
  }

  [[deprecated(
      "Participant::setAttribute is an internal cache mutator (does not sync with the server) and will be "
      "removed from the public API; use LocalParticipant::setAttributes for server-synced updates")]]
  void setAttribute(const std::string& key, const std::string& value) {
    setAttributeInternal(key, value);
  }

  [[deprecated(
      "Participant::removeAttribute is an internal cache mutator (does not sync with the server) and will be "
      "removed from the public API; use LocalParticipant::setAttributes for server-synced updates")]]
  void removeAttribute(const std::string& key) {
    removeAttributeInternal(key);
  }

  [[deprecated(
      "Participant::setKind is an internal cache mutator (Kind is server-determined and not "
      "user-settable) and will be removed from the public API")]]
  void setKind(ParticipantKind kind) noexcept {
    setKindInternal(kind);
  }

  [[deprecated(
      "Participant::setDisconnectReason is an internal cache mutator (DisconnectReason is "
      "server-determined and not user-settable) and will be removed from the public API")]]
  void setDisconnectReason(DisconnectReason reason) noexcept {
    setDisconnectReasonInternal(reason);
  }

  // NOLINTBEGIN(readability-identifier-naming)

  // Deprecated - see setName() (also deprecated; see notes above).
  [[deprecated("Participant::set_name is deprecated; use LocalParticipant::setName instead")]]
  void set_name(std::string name) noexcept {
    setNameInternal(std::move(name));
  }

  // Deprecated - see setMetadata() (also deprecated; see notes above).
  [[deprecated("Participant::set_metadata is deprecated; use LocalParticipant::setMetadata instead")]]
  void set_metadata(std::string metadata) noexcept {
    setMetadataInternal(std::move(metadata));
  }

  // Deprecated - see setAttributes() (also deprecated; see notes above).
  [[deprecated("Participant::set_attributes is deprecated; use LocalParticipant::setAttributes instead")]]
  void set_attributes(std::unordered_map<std::string, std::string> attrs) noexcept {
    setAttributesInternal(std::move(attrs));
  }

  // Deprecated - see setAttribute() (also deprecated; see notes above).
  [[deprecated("Participant::set_attribute is deprecated; use LocalParticipant::setAttributes instead")]]
  void set_attribute(const std::string& key, const std::string& value) {
    setAttributeInternal(key, value);
  }

  // Deprecated - see removeAttribute() (also deprecated; see notes above).
  [[deprecated("Participant::remove_attribute is deprecated; use LocalParticipant::setAttributes instead")]]
  void remove_attribute(const std::string& key) {
    removeAttributeInternal(key);
  }

  // Deprecated - see setKind() (also deprecated; see notes above).
  [[deprecated("Participant::set_kind is deprecated; Kind is server-determined and not user-settable")]]
  void set_kind(ParticipantKind kind) noexcept {
    setKindInternal(kind);
  }

  // Deprecated - see setDisconnectReason() (also deprecated; see notes above).
  [[deprecated(
      "Participant::set_disconnect_reason is deprecated; DisconnectReason is server-determined and not "
      "user-settable")]]
  void set_disconnect_reason(DisconnectReason reason) noexcept {
    setDisconnectReasonInternal(reason);
  }

  // NOLINTEND(readability-identifier-naming)

protected:
  // Internal cache mutators. Not part of the public SDK API.
  //
  // Used by Room (a friend of Participant) to mirror state pushed from the
  // LiveKit server, and by derived classes that need to update cached state
  // after a successful server-side update. These never contact the server.
  void setNameInternal(std::string name) noexcept { name_ = std::move(name); }
  void setMetadataInternal(std::string metadata) noexcept { metadata_ = std::move(metadata); }
  void setAttributesInternal(std::unordered_map<std::string, std::string> attrs) noexcept {
    attributes_ = std::move(attrs);
  }
  void setAttributeInternal(const std::string& key, const std::string& value) { attributes_[key] = value; }
  void removeAttributeInternal(const std::string& key) { attributes_.erase(key); }
  void setKindInternal(ParticipantKind kind) noexcept { kind_ = kind; }
  void setDisconnectReasonInternal(DisconnectReason reason) noexcept { reason_ = reason; }

  virtual std::shared_ptr<TrackPublication> findTrackPublication(const std::string& sid) const = 0;
  friend class Room;

private:
  FfiHandle handle_;
  std::string sid_, name_, identity_, metadata_;
  std::unordered_map<std::string, std::string> attributes_;
  ParticipantKind kind_;
  DisconnectReason reason_;
};

} // namespace livekit