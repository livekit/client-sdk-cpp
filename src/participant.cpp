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

#include <memory>

#include "ffi.pb.h"
#include "livekit/ffi_client.h"
#include "livekit/room.h"
#include "track.pb.h"

namespace livekit {
void LocalParticipant::PublishTrack(std::shared_ptr<Track> track,
                                    const proto::TrackPublishOptions& options) {
  std::cout << "[Hengstar] publish track" << std::endl;
  // TODO: Add audio track support
  if (track->Getkind() == proto::TrackKind::KIND_AUDIO) {
    throw std::runtime_error("cannot publish a remote track");
  }

  proto::FfiRequest request{};
  proto::PublishTrackRequest* publishTrackRequest =
      request.mutable_publish_track();
  publishTrackRequest->mutable_track_handle()->set_id(
      track->ffiHandle_.GetHandle());
  publishTrackRequest->mutable_room_handle()->set_id(
      room_->handle_.GetHandle());
  *publishTrackRequest->mutable_options() = options;

  proto::PublishTrackResponse resp =
      FfiClient::getInstance().SendRequest(request).publish_track();
  publishAsyncId_ = resp.async_id();

  listenerId_ = FfiClient::getInstance().AddListener(
      std::bind(&LocalParticipant::OnEvent, this, std::placeholders::_1));
  // std::unique_lock lock(lock_);

  // cv_.wait(lock, [this] { return publishCallback_ != nullptr; });
  std::cout << "[Hengstar] publish track done" << std::endl;
  // TODO: Handle errors
}

void LocalParticipant::OnEvent(const proto::FfiEvent& event) {
  std::cout << "[Hengstar] got event for PublishTrack" << std::endl;
  if (event.has_publish_track()) {
    std::cout << "[Hengstar] got publish track event" << std::endl;
    proto::PublishTrackCallback cb = event.publish_track();
    if (cb.async_id().id() == publishAsyncId_.id()) {
      std::cout << "[Hengstar] got publish track callback" << std::endl;
      publishCallback_ = std::make_unique<proto::PublishTrackCallback>(cb);
      // FfiClient::getInstance().RemoveListener(listenerId_);
    }
  }
}
}  // namespace livekit
