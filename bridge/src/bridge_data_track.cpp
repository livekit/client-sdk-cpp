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

#include "livekit_bridge/bridge_data_track.h"

#include "livekit/data_track_frame.h"
#include "livekit/local_data_track.h"

#include <iostream>

namespace livekit_bridge {

BridgeDataTrack::BridgeDataTrack(std::string name,
                                 std::shared_ptr<livekit::LocalDataTrack> track)
    : name_(std::move(name)), track_(std::move(track)) {}

BridgeDataTrack::~BridgeDataTrack() { release(); }

bool BridgeDataTrack::pushFrame(
    const std::vector<std::uint8_t> &payload,
    std::optional<std::uint64_t> user_timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (released_ || !track_) {
    return false;
  }

  livekit::DataTrackFrame frame;
  frame.payload = payload;
  frame.user_timestamp = user_timestamp;

  return track_->tryPush(frame);
}

bool BridgeDataTrack::pushFrame(
    const std::uint8_t *data, std::size_t size,
    std::optional<std::uint64_t> user_timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (released_ || !track_) {
    return false;
  }

  livekit::DataTrackFrame frame;
  frame.payload.assign(data, data + size);
  frame.user_timestamp = user_timestamp;

  return track_->tryPush(frame);
}

bool BridgeDataTrack::isPublished() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (released_ || !track_) {
    return false;
  }
  return track_->isPublished();
}

bool BridgeDataTrack::isReleased() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return released_;
}

void BridgeDataTrack::release() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (released_) {
    return;
  }
  released_ = true;

  if (track_) {
    try {
      track_->unpublish();
    } catch (...) {
      std::cerr << "[BridgeDataTrack] unpublish error, continuing with "
                   "cleanup\n";
    }
  }

  track_.reset();
}

} // namespace livekit_bridge
