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

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "livekit/ffi_handle.h"
#include "livekit_ffi.h"

namespace livekit {

enum class ParticipantKind { Standard = 0, Ingress, Egress, Sip, Agent };

enum class DisconnectReason {
  Unknown = 0,
  ClientInitiated,
  DuplicateIdentity,
  ServerShutdown,
  ParticipantRemoved,
  RoomDeleted,
  StateMismatch,
  JoinFailure,
  Migration,
  SignalClose,
  RoomClosed,
  UserUnavailable,
  UserRejected,
  SipTrunkFailure,
  ConnectionTimeout,
  MediaFailure
};

class Participant {
public:
  // TODO, consider holding a weak ptr of FfiHandle if it is useful.
  Participant(std::weak_ptr<FfiHandle> handle, std::string sid,
              std::string name, std::string identity, std::string metadata,
              std::unordered_map<std::string, std::string> attributes,
              ParticipantKind kind, DisconnectReason reason)
      : handle_(handle), sid_(std::move(sid)), name_(std::move(name)),
        identity_(std::move(identity)), metadata_(std::move(metadata)),
        attributes_(std::move(attributes)), kind_(kind), reason_(reason) {}

  // Plain getters/setters (caller ensures threading)
  const std::string &sid() const noexcept { return sid_; }
  const std::string &name() const noexcept { return name_; }
  const std::string &identity() const noexcept { return identity_; }
  const std::string &metadata() const noexcept { return metadata_; }
  const std::unordered_map<std::string, std::string> &
  attributes() const noexcept {
    return attributes_;
  }
  ParticipantKind kind() const noexcept { return kind_; }
  DisconnectReason disconnectReason() const noexcept { return reason_; }

  uintptr_t ffiHandleId() const noexcept {
    if (auto h = handle_.lock()) {
      return h->get();
    }
    return INVALID_HANDLE;
  }

private:
  std::weak_ptr<FfiHandle> handle_;
  std::string sid_, name_, identity_, metadata_;
  std::unordered_map<std::string, std::string> attributes_;
  ParticipantKind kind_;
  DisconnectReason reason_;
};

} // namespace livekit