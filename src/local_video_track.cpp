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

#include "livekit/local_video_track.h"

#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/video_source.h"
#include "track.pb.h"
#include "track_proto_converter.h"

namespace livekit {

LocalVideoTrack::LocalVideoTrack(FfiHandle handle,
                                 const proto::OwnedTrack &track,
                                 std::shared_ptr<VideoSource> video_source)
    : Track(std::move(handle), track.info().sid(), track.info().name(),
            fromProto(track.info().kind()),
            fromProto(track.info().stream_state()), track.info().muted(),
            false),
      video_source_(std::move(video_source)) {}

std::shared_ptr<LocalVideoTrack> LocalVideoTrack::createLocalVideoTrack(
    const std::string &name, const std::shared_ptr<VideoSource> &source) {
  proto::FfiRequest req;
  auto *msg = req.mutable_create_video_track();
  msg->set_name(name);
  msg->set_source_handle(static_cast<uint64_t>(source->ffi_handle_id()));

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  const proto::OwnedTrack &owned = resp.create_video_track().track();
  FfiHandle handle(static_cast<uintptr_t>(owned.handle().id()));
  return std::shared_ptr<LocalVideoTrack>(
      new LocalVideoTrack(std::move(handle), owned, std::move(source)));
}

std::shared_ptr<LocalVideoTrack>
LocalVideoTrack::createLocalVideoTrack(const std::string &name, const int width,
                                       const int height,
                                       const TrackSource track_source) {
  auto video_source = std::make_shared<VideoSource>(width, height);
  proto::FfiRequest req;
  auto *msg = req.mutable_create_video_track();
  msg->set_name(name);
  msg->set_source_handle(static_cast<uint64_t>(video_source->ffi_handle_id()));

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  const proto::OwnedTrack &owned_track = resp.create_video_track().track();
  FfiHandle handle(static_cast<uintptr_t>(owned_track.handle().id()));
  auto track = std::shared_ptr<LocalVideoTrack>(new LocalVideoTrack(
      std::move(handle), owned_track, std::move(video_source)));
  track->setPublicationFields(track_source, std::nullopt,
                              static_cast<std::uint32_t>(width),
                              static_cast<std::uint32_t>(height), std::nullopt);
  return track;
}

void LocalVideoTrack::captureFrame(const VideoFrame &frame,
                                   std::int64_t timestamp_us,
                                   VideoRotation rotation) {
  if (!video_source_) {
    return;
  }
  video_source_->captureFrame(frame, timestamp_us, rotation);
}

void LocalVideoTrack::mute() {
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

void LocalVideoTrack::unmute() {
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

std::string LocalVideoTrack::to_string() const {
  return "rtc.LocalVideoTrack(sid=" + sid() + ", name=" + name() + ")";
}

} // namespace livekit