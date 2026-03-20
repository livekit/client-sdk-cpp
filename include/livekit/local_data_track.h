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
#include "livekit/data_track_info.h"
#include "livekit/ffi_handle.h"

#include <memory>
#include <string>

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
 *   auto dt = lp->publishDataTrack("sensor-data");
 *   DataFrame frame;
 *   frame.payload = {0x01, 0x02, 0x03};
 *   dt->tryPush(frame);
 *   lp->unpublishDataTrack(dt);
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
   * @return true on success, false if the push failed (e.g. back-pressure
   *         or the track has been unpublished).
   */
  bool tryPush(const DataFrame &frame);

  /// Whether the track is still published in the room.
  bool isPublished() const;

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
