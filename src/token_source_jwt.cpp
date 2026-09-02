/*
 * Copyright 2026 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the License governing permissions and limitations.
 */

#include <chrono>
#include <cstdint>
#include <exception>
#include <livekit_uniffi.hpp>
#include <string>

#include "token_source_internal.h"

namespace livekit {

bool isParticipantTokenValid(const std::string& participant_token) {
  try {
    const auto claims = livekit_uniffi::token_claims_from_unverified(participant_token);
    constexpr std::uint64_t kExpiryBufferSeconds = 60;
    const auto now_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (now_seconds < 0) {
      return false;
    }
    return claims.exp > static_cast<std::uint64_t>(now_seconds) + kExpiryBufferSeconds;
  } catch (const std::exception&) {
    return false;
  }
}

} // namespace livekit
