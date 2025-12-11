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
#include <vector>

#include "livekit/ffi_handle.h"
#include "livekit/track.h"

namespace livekit {

// TODO, move this EncryptionType to e2ee_types.h
enum class EncryptionType {
  NONE = 0,
  GCM = 1,
  CUSTOM = 2,
};

class Track;
class LocalTrack;
class RemoteTrack;

/**
 * C++ TrackPublication.
 *
 * Wraps the immutable publication info plus an FFI handle, and
 * holds a weak reference to the associated Track (if any).
 */
class TrackPublication {
public:
  virtual ~TrackPublication() = default;

  TrackPublication(const TrackPublication &) = delete;
  TrackPublication &operator=(const TrackPublication &) = delete;
  TrackPublication(TrackPublication &&) noexcept = default;
  TrackPublication &operator=(TrackPublication &&) noexcept = default;

  // Basic metadata
  const std::string &sid() const noexcept { return sid_; }
  const std::string &name() const noexcept { return name_; }
  TrackKind kind() const noexcept { return kind_; }
  TrackSource source() const noexcept { return source_; }
  bool simulcasted() const noexcept { return simulcasted_; }
  std::uint32_t width() const noexcept { return width_; }
  std::uint32_t height() const noexcept { return height_; }
  const std::string &mimeType() const noexcept { return mime_type_; }
  bool muted() const noexcept { return muted_; }
  void setMuted(bool muted) noexcept { muted_ = muted; }

  EncryptionType encryptionType() const noexcept { return encryption_type_; }
  const std::vector<AudioTrackFeature> &audioFeatures() const noexcept {
    return audio_features_;
  }

  /// Underlying FFI handle value.
  uintptr_t ffiHandleId() const noexcept { return handle_.get(); }

  /// Associated Track (if attached).
  std::shared_ptr<Track> track() const noexcept { return track_; }
  void setTrack(const std::shared_ptr<Track> &track) noexcept {
    track_ = track;
  }

protected:
  TrackPublication(FfiHandle handle, std::string sid, std::string name,
                   TrackKind kind, TrackSource source, bool simulcasted,
                   std::uint32_t width, std::uint32_t height,
                   std::string mime_type, bool muted,
                   EncryptionType encryption_type,
                   std::vector<AudioTrackFeature> audio_features);

  FfiHandle handle_;
  std::shared_ptr<Track> track_;

  std::string sid_;
  std::string name_;
  TrackKind kind_;
  TrackSource source_;
  bool simulcasted_{false};
  std::uint32_t width_{0};
  std::uint32_t height_{0};
  std::string mime_type_;
  bool muted_{false};
  EncryptionType encryption_type_;
  std::vector<AudioTrackFeature> audio_features_;
};

} // namespace livekit
