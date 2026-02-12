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

#include "livekit_bridge/bridge_video_track.h"

#include "livekit/local_participant.h"
#include "livekit/local_track_publication.h"
#include "livekit/local_video_track.h"
#include "livekit/video_frame.h"
#include "livekit/video_source.h"

#include <stdexcept>

namespace livekit_bridge {

BridgeVideoTrack::BridgeVideoTrack(
    std::string name, int width, int height,
    std::shared_ptr<livekit::VideoSource> source,
    std::shared_ptr<livekit::LocalVideoTrack> track,
    std::shared_ptr<livekit::LocalTrackPublication> publication,
    livekit::LocalParticipant *participant)
    : name_(std::move(name)), width_(width), height_(height),
      source_(std::move(source)), track_(std::move(track)),
      publication_(std::move(publication)), participant_(participant) {}

BridgeVideoTrack::~BridgeVideoTrack() { release(); }

void BridgeVideoTrack::pushFrame(const std::vector<std::uint8_t> &data,
                                 std::int64_t timestamp_us) {
  if (released_) {
    throw std::runtime_error(
        "BridgeVideoTrack::pushFrame: track has been released");
  }

  livekit::VideoFrame frame(width_, height_, livekit::VideoBufferType::RGBA,
                            std::vector<std::uint8_t>(data.begin(), data.end()));
  source_->captureFrame(frame, timestamp_us);
}

void BridgeVideoTrack::pushFrame(const std::uint8_t *data,
                                 std::size_t data_size,
                                 std::int64_t timestamp_us) {
  if (released_) {
    throw std::runtime_error(
        "BridgeVideoTrack::pushFrame: track has been released");
  }

  livekit::VideoFrame frame(width_, height_, livekit::VideoBufferType::RGBA,
                            std::vector<std::uint8_t>(data, data + data_size));
  source_->captureFrame(frame, timestamp_us);
}

void BridgeVideoTrack::mute() {
  if (!released_ && track_) {
    track_->mute();
  }
}

void BridgeVideoTrack::unmute() {
  if (!released_ && track_) {
    track_->unmute();
  }
}

void BridgeVideoTrack::release() {
  if (released_) {
    return;
  }
  released_ = true;

  // Unpublish the track from the room
  if (participant_ && publication_) {
    try {
      participant_->unpublishTrack(publication_->sid());
    } catch (...) {
      // Best-effort cleanup; ignore errors during teardown
    }
  }

  // Release SDK objects in reverse order
  publication_.reset();
  track_.reset();
  source_.reset();
  participant_ = nullptr;
}

} // namespace livekit_bridge
