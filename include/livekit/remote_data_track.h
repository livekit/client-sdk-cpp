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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "livekit/data_track_info.h"
#include "livekit/data_track_subscription.h"
#include "livekit/ffi_handle.h"

#include <memory>
#include <string>

namespace livekit {

namespace proto {
class OwnedRemoteDataTrack;
}

/**
 * Represents a data track published by a remote participant.
 *
 * Discovered via the RemoteDataTrackPublishedEvent room event. Unlike
 * audio/video tracks, remote data tracks require an explicit subscribe()
 * call to begin receiving frames.
 *
 * Typical usage:
 *
 *   // In RoomDelegate::onRemoteDataTrackPublished callback:
 *   auto sub = remoteDataTrack->subscribe();
 *   DataTrackFrame frame;
 *   while (sub->read(frame)) {
 *     // process frame
 *   }
 */
class RemoteDataTrack {
public:
  ~RemoteDataTrack() = default;

  RemoteDataTrack(const RemoteDataTrack &) = delete;
  RemoteDataTrack &operator=(const RemoteDataTrack &) = delete;

  /// Metadata about this data track.
  const DataTrackInfo &info() const noexcept { return info_; }

  /// Identity of the remote participant who published this track.
  const std::string &publisherIdentity() const noexcept {
    return publisher_identity_;
  }

  /// Raw FFI handle id for internal use.
  uintptr_t ffi_handle_id() const noexcept { return handle_.get(); }

  /// Whether the track is still published by the remote participant.
  bool isPublished() const;

  /**
   * Subscribe to this remote data track.
   *
   * Returns a DataTrackSubscription that delivers frames via blocking
   * read(). Destroy the subscription to unsubscribe.
   *
   * @throws std::runtime_error if the FFI subscribe call fails.
   */
  std::shared_ptr<DataTrackSubscription>
  subscribe(const DataTrackSubscription::Options &options = {});

  /// Construct from an owned proto (called by Room event handling).
  explicit RemoteDataTrack(const proto::OwnedRemoteDataTrack &owned);

private:
  /** RAII wrapper for the Rust-owned FFI resource. */
  FfiHandle handle_;

  /** Metadata snapshot taken at construction time. */
  DataTrackInfo info_;

  /** Identity string of the remote participant who published this track. */
  std::string publisher_identity_;
};

} // namespace livekit
