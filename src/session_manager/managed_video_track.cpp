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

/// @file managed_video_track.cpp
/// @brief Implementation of ManagedVideoTrack.

#include "session_manager/managed_video_track.h"

#include "livekit/local_participant.h"
#include "livekit/local_track_publication.h"
#include "livekit/local_video_track.h"
#include "livekit/video_frame.h"
#include "livekit/video_source.h"

#include <stdexcept>

#include "lk_log.h"

namespace session_manager {

ManagedVideoTrack::ManagedVideoTrack(
    std::string name, int width, int height,
    std::shared_ptr<livekit::VideoSource> source,
    std::shared_ptr<livekit::LocalVideoTrack> track,
    std::shared_ptr<livekit::LocalTrackPublication> publication,
    livekit::LocalParticipant *participant)
    : name_(std::move(name)), width_(width), height_(height),
      source_(std::move(source)), track_(std::move(track)),
      publication_(std::move(publication)), participant_(participant) {}

ManagedVideoTrack::~ManagedVideoTrack() { release(); }

bool ManagedVideoTrack::pushFrame(const std::vector<std::uint8_t> &rgba,
                                  std::int64_t timestamp_us) {
  livekit::VideoFrame frame(
      width_, height_, livekit::VideoBufferType::RGBA,
      std::vector<std::uint8_t>(rgba.begin(), rgba.end()));

  std::lock_guard<std::mutex> lock(mutex_);
  if (released_) {
    return false;
  }

  try {
    source_->captureFrame(frame, timestamp_us);
  } catch (const std::exception &e) {
    LK_LOG_ERROR("ManagedVideoTrack captureFrame error: {}", e.what());
    return false;
  }
  return true;
}

bool ManagedVideoTrack::pushFrame(const std::uint8_t *rgba,
                                  std::size_t rgba_size,
                                  std::int64_t timestamp_us) {
  livekit::VideoFrame frame(width_, height_, livekit::VideoBufferType::RGBA,
                            std::vector<std::uint8_t>(rgba, rgba + rgba_size));

  std::lock_guard<std::mutex> lock(mutex_);
  if (released_) {
    return false;
  }

  try {
    source_->captureFrame(frame, timestamp_us);
  } catch (const std::exception &e) {
    LK_LOG_ERROR("ManagedVideoTrack captureFrame error: {}", e.what());
    return false;
  }
  return true;
}

void ManagedVideoTrack::mute() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!released_ && track_) {
    track_->mute();
  }
}

void ManagedVideoTrack::unmute() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!released_ && track_) {
    track_->unmute();
  }
}

bool ManagedVideoTrack::isReleased() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return released_;
}

void ManagedVideoTrack::release() {
  std::lock_guard<std::mutex> lock(mutex_);
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
      LK_LOG_WARN("ManagedVideoTrack unpublishTrack error, continuing with "
                  "cleanup");
    }
  }

  // Release SDK objects in reverse order
  publication_.reset();
  track_.reset();
  source_.reset();
  participant_ = nullptr;
}

} // namespace session_manager
