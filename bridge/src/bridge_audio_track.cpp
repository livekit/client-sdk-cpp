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

#include "livekit_bridge/bridge_audio_track.h"

#include "livekit/audio_frame.h"
#include "livekit/audio_source.h"
#include "livekit/local_audio_track.h"
#include "livekit/local_participant.h"
#include "livekit/local_track_publication.h"

namespace livekit_bridge {

BridgeAudioTrack::BridgeAudioTrack(
    std::string name, int sample_rate, int num_channels,
    std::shared_ptr<livekit::AudioSource> source,
    std::shared_ptr<livekit::LocalAudioTrack> track,
    std::shared_ptr<livekit::LocalTrackPublication> publication,
    livekit::LocalParticipant *participant)
    : name_(std::move(name)), sample_rate_(sample_rate),
      num_channels_(num_channels), source_(std::move(source)),
      track_(std::move(track)), publication_(std::move(publication)),
      participant_(participant) {}

BridgeAudioTrack::~BridgeAudioTrack() { release(); }

bool BridgeAudioTrack::pushFrame(const std::vector<std::int16_t> &data,
                                 int samples_per_channel, int timeout_ms) {
  livekit::AudioFrame frame(std::vector<std::int16_t>(data.begin(), data.end()),
                            sample_rate_, num_channels_, samples_per_channel);

  std::lock_guard<std::mutex> lock(mutex_);
  if (released_) {
    return false;
  }

  source_->captureFrame(frame, timeout_ms);
  return true;
}

bool BridgeAudioTrack::pushFrame(const std::int16_t *data,
                                 int samples_per_channel, int timeout_ms) {
  const int total_samples = samples_per_channel * num_channels_;
  livekit::AudioFrame frame(
      std::vector<std::int16_t>(data, data + total_samples), sample_rate_,
      num_channels_, samples_per_channel);

  std::lock_guard<std::mutex> lock(mutex_);
  if (released_) {
    return false;
  }

  source_->captureFrame(frame, timeout_ms);
  return true;
}

void BridgeAudioTrack::mute() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!released_ && track_) {
    track_->mute();
  }
}

void BridgeAudioTrack::unmute() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!released_ && track_) {
    track_->unmute();
  }
}

bool BridgeAudioTrack::isReleased() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return released_;
}

void BridgeAudioTrack::release() {
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
    }
  }

  // Release SDK objects in reverse order
  publication_.reset();
  track_.reset();
  source_.reset();
  participant_ = nullptr;
}

} // namespace livekit_bridge
