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

/// @file bridge_room_delegate.h
/// @brief Internal RoomDelegate forwarding SDK events to SessionManager.

#pragma once

#include "livekit/room_delegate.h"

namespace livekit_bridge {

class SessionManager;

/**
 * Internal RoomDelegate that forwards SDK room events to the SessionManager.
 *
 * Handles track subscribe/unsubscribe lifecycle. Not part of the public API,
 * so its in src/ instead of include/.
 */
class BridgeRoomDelegate : public livekit::RoomDelegate {
public:
  explicit BridgeRoomDelegate(SessionManager &bridge) : bridge_(bridge) {}

  /// Forwards a track-subscribed event to SessionManager::onTrackSubscribed().
  void onTrackSubscribed(livekit::Room &room,
                         const livekit::TrackSubscribedEvent &ev) override;

  /// Forwards a track-unsubscribed event to
  /// SessionManager::onTrackUnsubscribed().
  void onTrackUnsubscribed(livekit::Room &room,
                           const livekit::TrackUnsubscribedEvent &ev) override;

private:
  SessionManager &bridge_;
};

} // namespace livekit_bridge
