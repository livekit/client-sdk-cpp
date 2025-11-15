/*
 * Copyright 2023 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the “License”);
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
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <future>
#include "livekit/ffi_handle.h"
#include "livekit/stats.h"

namespace livekit {

// ----- Enums from track.proto -----
enum class TrackKind {
  KIND_UNKNOWN = 0,
  KIND_AUDIO   = 1,
  KIND_VIDEO   = 2,
};

enum class TrackSource {
  SOURCE_UNKNOWN           = 0,
  SOURCE_CAMERA            = 1,
  SOURCE_MICROPHONE        = 2,
  SOURCE_SCREENSHARE       = 3,
  SOURCE_SCREENSHARE_AUDIO = 4,
};

enum class StreamState {
  STATE_UNKNOWN = 0,
  STATE_ACTIVE  = 1,
  STATE_PAUSED  = 2,
};

enum class AudioTrackFeature {
  TF_STEREO                        = 0,
  TF_NO_DTX                        = 1,
  TF_AUTO_GAIN_CONTROL             = 2,
  TF_ECHO_CANCELLATION             = 3,
  TF_NOISE_SUPPRESSION             = 4,
  TF_ENHANCED_NOISE_CANCELLATION   = 5,
  TF_PRECONNECT_BUFFER             = 6,
};


// ============================================================
// Base Track
// ============================================================
class Track {
public:
  virtual ~Track() = default;

  // Read-only properties
  const std::string& sid()   const noexcept { return sid_; }
  const std::string& name()  const noexcept { return name_; }
  TrackKind kind()           const noexcept { return kind_; }
  StreamState stream_state() const noexcept { return state_; }
  bool muted()               const noexcept { return muted_; }
  bool remote()              const noexcept { return remote_; }

  // Optional publication info
  std::optional<TrackSource>   source() const noexcept { return source_; }
  std::optional<bool>          simulcasted()  const noexcept { return simulcasted_; }
  std::optional<uint32_t>      width()        const noexcept { return width_; }
  std::optional<uint32_t>      height()       const noexcept { return height_; }
  std::optional<std::string>   mime_type()    const noexcept { return mime_type_; }

  // Handle access
  bool has_handle() const noexcept { return !handle_.expired(); }
  uintptr_t ffi_handle_id() const noexcept {
    if (auto h = handle_.lock()) return h->get();
    return 0;
  }
  std::shared_ptr<const FfiHandle> lock_handle() const noexcept { return handle_.lock(); }

  // Async get stats
  std::future<std::vector<RtcStats>> getStats() const;

  // Internal updates (called by Room)
  void setStreamState(StreamState s) noexcept { state_ = s; }
  void setMuted(bool m) noexcept { muted_ = m; }
  void setName(std::string n) noexcept { name_ = std::move(n); }

protected:
  Track(std::weak_ptr<FfiHandle> handle,
        std::string sid,
        std::string name,
        TrackKind kind,
        StreamState state,
        bool muted,
        bool remote);

  void setPublicationFields(std::optional<TrackSource> source,
                            std::optional<bool> simulcasted,
                            std::optional<uint32_t> width,
                            std::optional<uint32_t> height,
                            std::optional<std::string> mime_type);

private:
  std::weak_ptr<FfiHandle> handle_;  // non-owning

  std::string sid_;
  std::string name_;
  TrackKind   kind_{TrackKind::KIND_UNKNOWN};
  StreamState state_{StreamState::STATE_UNKNOWN};
  bool muted_{false};
  bool remote_{false};

  std::optional<TrackSource> source_;
  std::optional<bool> simulcasted_;
  std::optional<uint32_t> width_;
  std::optional<uint32_t> height_;
  std::optional<std::string> mime_type_;
};

}  // namespace livekit