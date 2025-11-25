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

#include "livekit/remote_audio_track.h"

#include "ffi.pb.h"
#include "livekit/audio_source.h"
#include "livekit/ffi_client.h"
#include "track.pb.h"
#include "track_proto_converter.h"

namespace livekit {

RemoteAudioTrack::RemoteAudioTrack(FfiHandle handle,
                                   const proto::OwnedTrack &track)
    : Track(std::move(handle), track.info().sid(), track.info().name(),
            fromProto(track.info().kind()),
            fromProto(track.info().stream_state()), track.info().muted(),
            true) {}

std::shared_ptr<RemoteAudioTrack> RemoteAudioTrack::createRemoteAudioTrack(
    const std::string &name, const std::shared_ptr<AudioSource> &source) {
  proto::FfiRequest req;
  auto *msg = req.mutable_create_audio_track();
  msg->set_name(name);
  msg->set_source_handle(static_cast<uint64_t>(source->ffi_handle_id()));

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  const proto::OwnedTrack &owned = resp.create_audio_track().track();
  FfiHandle handle(static_cast<uintptr_t>(owned.handle().id()));
  return std::make_shared<RemoteAudioTrack>(std::move(handle), owned);
}

std::string RemoteAudioTrack::to_string() const {
  return "rtc.RemoteAudioTrack(sid=" + sid() + ", name=" + name() + ")";
}

} // namespace livekit