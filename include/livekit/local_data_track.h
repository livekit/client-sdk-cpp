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

#include "livekit/data_frame.h"
#include "livekit/data_track_error.h"
#include "livekit/data_track_info.h"
#include "livekit/ffi_handle.h"
#include "livekit/result.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace livekit {

namespace proto {
class OwnedLocalDataTrack;
}

/**
 * Represents a locally published data track.
 *
 * Unlike audio/video tracks, data tracks do not extend the Track base class.
 * They use a separate publish/unpublish lifecycle and carry arbitrary binary
 * frames instead of media.
 *
 * Created via LocalParticipant::publishDataTrack().
 *
 * Typical usage:
 *
 *   auto lp = room->localParticipant();
 *   auto result = lp->publishDataTrack("sensor-data");
 *   if (result) {
 *     auto dt = result.value();
 *     DataFrame frame;
 *     frame.payload = {0x01, 0x02, 0x03};
 *     (void)dt->tryPush(frame);
 *     dt->unpublishDataTrack();
 *   }
 */
class LocalDataTrack {
public:
  ~LocalDataTrack() = default;

  LocalDataTrack(const LocalDataTrack &) = delete;
  LocalDataTrack &operator=(const LocalDataTrack &) = delete;

  /// Metadata about this data track.
  const DataTrackInfo &info() const noexcept { return info_; }

  /**
   * Try to push a frame to all subscribers of this track.
   *
   * @return success on delivery acceptance, or a typed error describing why
   *         the frame could not be queued.
   */
  Result<void, DataTrackError> tryPush(const DataFrame &frame);

  /**
   * Try to push a frame to all subscribers of this track.
   *
   * @return success on delivery acceptance, or a typed error describing why
   *         the frame could not be queued.
   */
  Result<void, DataTrackError>
  tryPush(const std::vector<std::uint8_t> &payload,
          std::optional<std::uint64_t> user_timestamp = std::nullopt);
  Result<void, DataTrackError>
  tryPush(std::vector<std::uint8_t> &&payload,
          std::optional<std::uint64_t> user_timestamp = std::nullopt);
  /**
   * Try to push a frame to all subscribers of this track.
   *
   * @return success on delivery acceptance, or a typed error describing why
   *         the frame could not be queued.
   */
  Result<void, DataTrackError>
  tryPush(const std::uint8_t *data, std::size_t size,
          std::optional<std::uint64_t> user_timestamp = std::nullopt);

  /// Whether the track is still published in the room.
  bool isPublished() const;

  /**
   * Unpublish this data track from the room.
   *
   * After this call, tryPush() fails and the track cannot be re-published.
   */
  void unpublishDataTrack();

private:
  friend class LocalParticipant;

  explicit LocalDataTrack(const proto::OwnedLocalDataTrack &owned);

  uintptr_t ffi_handle_id() const noexcept { return handle_.get(); }

  /** RAII wrapper for the Rust-owned FFI resource. */
  FfiHandle handle_;

  /** Metadata snapshot taken at construction time. */
  DataTrackInfo info_;
};

} // namespace livekit
