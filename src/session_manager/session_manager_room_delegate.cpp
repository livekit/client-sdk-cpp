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

/// @file session_manager_room_delegate.cpp
/// @brief Implementation of SessionManagerRoomDelegate event forwarding.

#include "session_manager_room_delegate.h"

#include "livekit/remote_participant.h"
#include "livekit/remote_track_publication.h"
#include "livekit/session_manager/session_manager.h"
#include "livekit/track.h"

namespace livekit {

void SessionManagerRoomDelegate::onTrackSubscribed(
    livekit::Room & /*room*/, const livekit::TrackSubscribedEvent &ev) {
  if (!ev.track || !ev.participant || !ev.publication) {
    return;
  }

  const std::string identity = ev.participant->identity();
  const livekit::TrackSource source = ev.publication->source();

  manager_.onTrackSubscribed(identity, source, ev.track);
}

void SessionManagerRoomDelegate::onTrackUnsubscribed(
    livekit::Room & /*room*/, const livekit::TrackUnsubscribedEvent &ev) {
  if (!ev.participant || !ev.publication) {
    return;
  }

  const std::string identity = ev.participant->identity();
  const livekit::TrackSource source = ev.publication->source();

  manager_.onTrackUnsubscribed(identity, source);
}

} // namespace livekit
