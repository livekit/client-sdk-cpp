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

#include <cstdint>
#include <stdexcept>

#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/audio_source.h"
#include "livekit/platform_audio.h"
#include "track.pb.h"
#include "track_proto_converter.h"

namespace livekit {

namespace {

proto::OwnedTrack createAudioTrackWithSourceHandle(const std::string& name, std::uint64_t source_handle) {
  proto::FfiRequest req;
  auto* msg = req.mutable_create_audio_track();
  msg->set_name(name);
  msg->set_source_handle(source_handle);

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_create_audio_track()) {
    // TODO(sderosa): we dont have an error code/return, is throwing ok?
    throw std::runtime_error("create_audio_track response is missing track");
  }
  return resp.create_audio_track().track();
}

} // namespace

LocalAudioTrack::LocalAudioTrack(FfiHandle handle, const proto::OwnedTrack& track)
    : Track(std::move(handle), track.info().sid(), track.info().name(), fromProto(track.info().kind()),
            fromProto(track.info().stream_state()), track.info().muted(), false) {}

std::shared_ptr<LocalAudioTrack> LocalAudioTrack::createLocalAudioTrack(const std::string& name,
                                                                        const std::shared_ptr<AudioSource>& source) {
  if (!source) {
    throw std::invalid_argument("LocalAudioTrack::createLocalAudioTrack: source is null");
  }

  const proto::OwnedTrack owned = createAudioTrackWithSourceHandle(name, source->ffiHandleId());
  FfiHandle handle(static_cast<uintptr_t>(owned.handle().id()));
  return std::shared_ptr<LocalAudioTrack>(new LocalAudioTrack(std::move(handle), owned));
}

std::shared_ptr<LocalAudioTrack> LocalAudioTrack::createLocalAudioTrack(
    const std::string& name, const std::shared_ptr<PlatformAudioSource>& source) {
  if (!source) {
    throw std::invalid_argument("LocalAudioTrack::createLocalAudioTrack: source is null");
  }

  const proto::OwnedTrack owned = createAudioTrackWithSourceHandle(name, source->ffiHandleId());
  FfiHandle handle(static_cast<uintptr_t>(owned.handle().id()));
  return std::shared_ptr<LocalAudioTrack>(new LocalAudioTrack(std::move(handle), owned));
}

void LocalAudioTrack::mute() {
  if (!hasHandle()) {
    setMuted(true);
    return;
  }

  proto::FfiRequest req;
  auto* msg = req.mutable_local_track_mute();
  msg->set_track_handle(static_cast<uint64_t>(ffiHandleId()));
  msg->set_mute(true);

  (void)FfiClient::instance().sendRequest(req);
  setMuted(true);
}

void LocalAudioTrack::unmute() {
  if (!hasHandle()) {
    setMuted(false);
    return;
  }

  proto::FfiRequest req;
  auto* msg = req.mutable_local_track_mute();
  msg->set_track_handle(static_cast<uint64_t>(ffiHandleId()));
  msg->set_mute(false);

  (void)FfiClient::instance().sendRequest(req);
  setMuted(false);
}

std::string LocalAudioTrack::toString() const { return "rtc.LocalAudioTrack(sid=" + sid() + ", name=" + name() + ")"; }

} // namespace livekit
