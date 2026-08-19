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

#include "livekit/remote_track_publication.h"

#include "ffi.pb.h"
#include "ffi_client.h"
#include "lk_log.h"
#include "track_proto_converter.h"

namespace livekit {

RemoteTrackPublication::RemoteTrackPublication(const proto::OwnedTrackPublication& owned)
    : TrackPublication(FfiHandle(owned.handle().id()), owned.info().sid(), owned.info().name(),
                       fromProto(owned.info().kind()), fromProto(owned.info().source()), owned.info().simulcasted(),
                       owned.info().width(), owned.info().height(), owned.info().mime_type(), owned.info().muted(),
                       static_cast<EncryptionType>(owned.info().encryption_type()),
                       convertAudioFeatures(owned.info().audio_features())) {}

void RemoteTrackPublication::setSubscribed(bool subscribed) {
  if (ffiHandleId() == 0) {
    throw std::runtime_error("RemoteTrackPublication::setSubscribed: invalid FFI handle");
  }

  proto::FfiRequest req;
  auto* msg = req.mutable_set_subscribed();
  msg->set_subscribe(subscribed);
  msg->set_publication_handle(static_cast<std::uint64_t>(ffiHandleId()));

  // Synchronous request; if you add an async version in FfiClient, you can
  // wire that up instead.
  auto resp = FfiClient::instance().sendRequest(req);
  (void)resp; // currently unused, but you can inspect error fields here

  subscribed_ = subscribed;
}

bool RemoteTrackPublication::setEnabled(bool enabled) {
  if (!subscribed_ || track() == nullptr) {
    LK_LOG_WARN("RemoteTrackPublication::setEnabled ignored for unsubscribed publication {}", sid());
    return false;
  }
  if (enabled_ == enabled) {
    return false;
  }
  if (ffiHandleId() == 0) {
    throw std::runtime_error("RemoteTrackPublication::setEnabled: invalid FFI handle");
  }

  proto::FfiRequest req;
  auto* msg = req.mutable_enable_remote_track_publication();
  msg->set_track_publication_handle(static_cast<std::uint64_t>(ffiHandleId()));
  msg->set_enabled(enabled);

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_enable_remote_track_publication()) {
    throw std::runtime_error("RemoteTrackPublication::setEnabled: FFI response missing enabled update result");
  }

  enabled_ = enabled;
  return true;
}

bool RemoteTrackPublication::setVideoQuality(VideoQuality quality) {
  if (kind() != TrackKind::KIND_VIDEO) {
    LK_LOG_WARN("RemoteTrackPublication::setVideoQuality ignored for non-video publication {}", sid());
    return false;
  }
  if (!simulcasted()) {
    LK_LOG_WARN("RemoteTrackPublication::setVideoQuality ignored for non-simulcast publication {}", sid());
    return false;
  }
  if (!subscribed_ || track() == nullptr) {
    LK_LOG_WARN("RemoteTrackPublication::setVideoQuality ignored for unsubscribed publication {}", sid());
    return false;
  }
  if (video_preference_ == VideoPreference::QUALITY && video_quality_ == quality) {
    return false;
  }

  proto::VideoQuality proto_quality;
  switch (quality) {
    case VideoQuality::LOW:
      proto_quality = proto::VideoQuality::VIDEO_QUALITY_LOW;
      break;
    case VideoQuality::MEDIUM:
      proto_quality = proto::VideoQuality::VIDEO_QUALITY_MEDIUM;
      break;
    case VideoQuality::HIGH:
      proto_quality = proto::VideoQuality::VIDEO_QUALITY_HIGH;
      break;
    default:
      LK_LOG_WARN("RemoteTrackPublication::setVideoQuality ignored invalid quality for publication {}", sid());
      return false;
  }
  if (ffiHandleId() == 0) {
    throw std::runtime_error("RemoteTrackPublication::setVideoQuality: invalid FFI handle");
  }

  proto::FfiRequest req;
  auto* msg = req.mutable_set_remote_track_publication_quality();
  msg->set_track_publication_handle(static_cast<std::uint64_t>(ffiHandleId()));
  msg->set_quality(proto_quality);

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_set_remote_track_publication_quality()) {
    throw std::runtime_error("RemoteTrackPublication::setVideoQuality: FFI response missing quality update result");
  }

  video_preference_ = VideoPreference::QUALITY;
  video_quality_ = quality;
  return true;
}

bool RemoteTrackPublication::setVideoDimensions(std::uint32_t width, std::uint32_t height) {
  if (kind() != TrackKind::KIND_VIDEO) {
    LK_LOG_WARN("RemoteTrackPublication::setVideoDimensions ignored for non-video publication {}", sid());
    return false;
  }
  if (width == 0 || height == 0) {
    LK_LOG_WARN("RemoteTrackPublication::setVideoDimensions requires non-zero dimensions for publication {}", sid());
    return false;
  }
  if (!subscribed_ || track() == nullptr) {
    LK_LOG_WARN("RemoteTrackPublication::setVideoDimensions ignored for unsubscribed publication {}", sid());
    return false;
  }
  if (video_preference_ == VideoPreference::DIMENSIONS && width_ == width && height_ == height) {
    return false;
  }
  if (ffiHandleId() == 0) {
    throw std::runtime_error("RemoteTrackPublication::setVideoDimensions: invalid FFI handle");
  }

  proto::FfiRequest req;
  auto* msg = req.mutable_update_remote_track_publication_dimension();
  msg->set_track_publication_handle(static_cast<std::uint64_t>(ffiHandleId()));
  msg->set_width(width);
  msg->set_height(height);

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_update_remote_track_publication_dimension()) {
    throw std::runtime_error(
        "RemoteTrackPublication::setVideoDimensions: FFI response missing dimension update result");
  }

  width_ = width;
  height_ = height;
  video_preference_ = VideoPreference::DIMENSIONS;
  video_quality_ = VideoQuality::HIGH;
  return true;
}

} // namespace livekit
