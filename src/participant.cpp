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

#include "livekit/participant.h"

#include "ffi.pb.h"
#include "livekit/ffi_client.h"
#include "livekit/room.h"
#include "track.pb.h"

namespace livekit {

LocalParticipant::~LocalParticipant() {
  FfiClient::getInstance().RemoveListener(listenerId_);
}

void LocalParticipant::PublishTrack(std::shared_ptr<Track> track,
                                    const proto::TrackPublishOptions& options) {
  std::cout << "[LocalParticipant] publish track" << std::endl;
  // TODO: Add audio track support
  if (track->Getkind() == proto::TrackKind::KIND_AUDIO) {
    throw std::runtime_error("cannot publish a remote track");
  }

  proto::FfiRequest request{};
  proto::PublishTrackRequest* publishTrackRequest = request.mutable_publish_track();
  publishTrackRequest->set_track_handle(track->ffiHandle_.GetHandleId());

  std::cout << "track->ffiHandle_.GetHandleId(): " << track->ffiHandle_.GetHandleId() << std::endl;
  *publishTrackRequest->mutable_options() = options;
  publishTrackRequest->set_local_participant_handle(handle_.GetHandleId());

  proto::PublishTrackResponse resp =
      FfiClient::getInstance().SendRequest(request).publish_track();
  publishAsyncId_ = resp.async_id();

  listenerId_ = FfiClient::getInstance().AddListener(
      std::bind(&LocalParticipant::OnEvent, this, std::placeholders::_1));

  // cv_.wait(lock, [this] { return publishCallback_ != nullptr; });
  std::cout << "[LocalParticipant] publish track done" << std::endl;
  // TODO: Handle errors
}

void LocalParticipant::OnEvent(const proto::FfiEvent& event) {
  std::cout << "[LocalParticipant] got event for PublishTrack" << std::endl;
  if (event.has_publish_track()) {
    std::cout << "[LocalParticipant] got publish track event" << std::endl;
    proto::PublishTrackCallback cb = event.publish_track();
    if (cb.async_id() == publishAsyncId_) {
      std::cout << "[LocalParticipant] got publish track callback" << std::endl;
      publishCallback_ = std::make_unique<proto::PublishTrackCallback>(cb);
    }
  }
}
}  // namespace livekit
