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

#include "bridge_room_delegate.h"

#include "livekit/remote_participant.h"
#include "livekit/remote_track_publication.h"
#include "livekit/track.h"
#include "livekit_bridge/livekit_bridge.h"

namespace livekit_bridge {

void BridgeRoomDelegate::onTrackSubscribed(
    livekit::Room & /*room*/, const livekit::TrackSubscribedEvent &ev) {
  if (!ev.track || !ev.participant || !ev.publication) {
    return;
  }

  const std::string identity = ev.participant->identity();
  const livekit::TrackSource source = ev.publication->source();

  bridge_.onTrackSubscribed(identity, source, ev.track);
}

void BridgeRoomDelegate::onTrackUnsubscribed(
    livekit::Room & /*room*/, const livekit::TrackUnsubscribedEvent &ev) {
  if (!ev.participant || !ev.publication) {
    return;
  }

  const std::string identity = ev.participant->identity();
  const livekit::TrackSource source = ev.publication->source();

  bridge_.onTrackUnsubscribed(identity, source);
}

} // namespace livekit_bridge
