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
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_set>

namespace livekit {

class BaseStreamWriter;

namespace detail {

/// @brief Tracks a LocalParticipant's stream writers so they can be closed when
/// the participant disconnects or is destroyed.
///
/// Shared between the participant and its writers, so a writer that outlives
/// its participant never touches the participant itself.
class StreamWriterRegistry {
public:
  /// @param participant_handle FFI handle id of the local participant.
  explicit StreamWriterRegistry(std::uint64_t participant_handle) : participant_handle_(participant_handle) {}

  /// @brief Register a writer.
  /// @return False if the participant is already disconnected.
  bool add(BaseStreamWriter* writer);

  /// @brief Unregister a writer. Blocks while closeAll() is running.
  void remove(BaseStreamWriter* writer);

  /// @brief Participant handle for a writer FFI request.
  /// @throws DataStreamError if the participant is disconnected.
  std::uint64_t participantHandle() const;

  /// @brief Mark the participant disconnected and close every registered
  /// writer. Idempotent.
  void closeAll() noexcept;

private:
  mutable std::mutex mutex_;
  std::uint64_t participant_handle_;
  bool closed_ = false;
  std::unordered_set<BaseStreamWriter*> writers_;
};

} // namespace detail
} // namespace livekit
