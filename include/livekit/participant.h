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
  // ---------------------------------------------------------------------------

  // NOLINTBEGIN(readability-identifier-naming)

  // Deprecated - see setName() (also deprecated; see notes above).
  [[deprecated("Participant::set_name is deprecated; use LocalParticipant::setName instead")]]
  void set_name(std::string name) noexcept {
    name_ = std::move(name);
  }

  // Deprecated - see setMetadata() (also deprecated; see notes above).
  [[deprecated("Participant::set_metadata is deprecated; use LocalParticipant::setMetadata instead")]]
  void set_metadata(std::string metadata) noexcept {
    metadata_ = std::move(metadata);
  }

  // Deprecated - see setAttributes() (also deprecated; see notes above).
  [[deprecated("Participant::set_attributes is deprecated; use LocalParticipant::setAttributes instead")]]
  void set_attributes(std::unordered_map<std::string, std::string> attrs) noexcept {
    attributes_ = std::move(attrs);
  }

  // Deprecated - see setAttribute() (also deprecated; see notes above).
  [[deprecated("Participant::set_attribute is deprecated; use LocalParticipant::setAttributes instead")]]
  void set_attribute(const std::string& key, const std::string& value) {
    attributes_[key] = value;
  }

  // Deprecated - see removeAttribute() (also deprecated; see notes above).
  [[deprecated("Participant::remove_attribute is deprecated; use LocalParticipant::setAttributes instead")]]
  void remove_attribute(const std::string& key) {
    attributes_.erase(key);
  }

  // Deprecated - see setKind() (also deprecated; see notes above).
  [[deprecated("Participant::set_kind is deprecated; Kind is server-determined and not user-settable")]]
  void set_kind(ParticipantKind kind) noexcept {
    kind_ = kind;
  }

  // Deprecated - see setDisconnectReason() (also deprecated; see notes above).
  [[deprecated(
      "Participant::set_disconnect_reason is deprecated; DisconnectReason is server-determined and not "
      "user-settable")]]
  void set_disconnect_reason(DisconnectReason reason) noexcept {
    reason_ = reason;
  }

  // NOLINTEND(readability-identifier-naming)

protected:
  virtual std::shared_ptr<TrackPublication> findTrackPublication(const std::string& sid) const = 0;

  /// Room class is a friend to set the internal state of the participant
  /// Avoids awkward additional setter methods
  friend class Room;

private:
  FfiHandle handle_;
  std::string sid_, name_, identity_, metadata_;
  std::unordered_map<std::string, std::string> attributes_;
  ParticipantKind kind_;
  DisconnectReason reason_;
};

} // namespace livekit