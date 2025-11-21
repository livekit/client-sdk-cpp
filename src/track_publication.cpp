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

#include "livekit/track_publication.h"

namespace livekit {

TrackPublication::TrackPublication(
    FfiHandle handle, std::string sid, std::string name, TrackKind kind,
    TrackSource source, bool simulcasted, std::uint32_t width,
    std::uint32_t height, std::string mime_type, bool muted,
    EncryptionType encryption_type,
    std::vector<AudioTrackFeature> audio_features)
    : handle_(std::move(handle)), sid_(std::move(sid)), name_(std::move(name)),
      kind_(kind), source_(source), simulcasted_(simulcasted), width_(width),
      height_(height), mime_type_(std::move(mime_type)), muted_(muted),
      encryption_type_(encryption_type),
      audio_features_(std::move(audio_features)) {}

} // namespace livekit
