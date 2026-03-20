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

#include "livekit/local_audio_track.h"

#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/audio_source.h"
#include "track.pb.h"
#include "track_proto_converter.h"

namespace livekit {

LocalAudioTrack::LocalAudioTrack(FfiHandle handle,
                                 const proto::OwnedTrack &track,
                                 std::shared_ptr<AudioSource> owned_source)
    : Track(std::move(handle), track.info().sid(), track.info().name(),
            fromProto(track.info().kind()),
            fromProto(track.info().stream_state()), track.info().muted(),
            false),
      owned_audio_source_(std::move(owned_source)) {}

std::shared_ptr<LocalAudioTrack> LocalAudioTrack::createLocalAudioTrack(
    const std::string &name, const std::shared_ptr<AudioSource> &source) {
  proto::FfiRequest req;
  auto *msg = req.mutable_create_audio_track();
  msg->set_name(name);
  msg->set_source_handle(static_cast<uint64_t>(source->ffi_handle_id()));

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  const proto::OwnedTrack &owned = resp.create_audio_track().track();
  FfiHandle handle(static_cast<uintptr_t>(owned.handle().id()));
  return std::shared_ptr<LocalAudioTrack>(
      new LocalAudioTrack(std::move(handle), owned, source));
}

std::shared_ptr<LocalAudioTrack> LocalAudioTrack::createLocalAudioTrack(
    const std::string &name, const int sample_rate, const int num_channels,
    const TrackSource source) {
  auto owned_source =
      std::make_shared<AudioSource>(sample_rate, num_channels, 0);
  proto::FfiRequest req;
  auto *msg = req.mutable_create_audio_track();
  msg->set_name(name);
  msg->set_source_handle(static_cast<uint64_t>(owned_source->ffi_handle_id()));

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  const proto::OwnedTrack &owned = resp.create_audio_track().track();
  FfiHandle handle(static_cast<uintptr_t>(owned.handle().id()));
  auto track = std::shared_ptr<LocalAudioTrack>(
      new LocalAudioTrack(std::move(handle), owned, std::move(owned_source)));
  track->setPublicationFields(source, std::nullopt, std::nullopt, std::nullopt,
                              std::nullopt);
  return track;
}

void LocalAudioTrack::captureFrame(const AudioFrame &frame, int timeout_ms) {
  if (!owned_audio_source_) {
    return;
  }
  owned_audio_source_->captureFrame(frame, timeout_ms);
}

void LocalAudioTrack::mute() {
  if (!has_handle()) {
    setMuted(true);
    return;
  }

  proto::FfiRequest req;
  auto *msg = req.mutable_local_track_mute();
  msg->set_track_handle(static_cast<uint64_t>(ffi_handle_id()));
  msg->set_mute(true);

  (void)FfiClient::instance().sendRequest(req);
  setMuted(true);
}

void LocalAudioTrack::unmute() {
  if (!has_handle()) {
    setMuted(false);
    return;
  }

  proto::FfiRequest req;
  auto *msg = req.mutable_local_track_mute();
  msg->set_track_handle(static_cast<uint64_t>(ffi_handle_id()));
  msg->set_mute(false);

  (void)FfiClient::instance().sendRequest(req);
  setMuted(false);
}

std::string LocalAudioTrack::to_string() const {
  return "rtc.LocalAudioTrack(sid=" + sid() + ", name=" + name() + ")";
}

} // namespace livekit