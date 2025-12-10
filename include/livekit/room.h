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

#ifndef LIVEKIT_ROOM_H
#define LIVEKIT_ROOM_H

#include "livekit/ffi_client.h"
#include "livekit/ffi_handle.h"
#include "livekit/room_event_types.h"
#include <memory>
#include <mutex>

namespace livekit {

class RoomDelegate;
struct RoomInfoData;
namespace proto {
class FfiEvent;
}

class LocalParticipant;
class RemoteParticipant;

// Represents end-to-end encryption (E2EE) settings.
// Mirrors python sdk: `E2EEOptions`
struct E2EEOptions {
  // Encryption algorithm type.
  int encryption_type = 0;

  // Shared static key. If provided, this key is used for encryption.
  std::string shared_key;

  // Salt used when deriving ratcheted encryption keys.
  std::string ratchet_salt;

  // How many consecutive ratcheting failures are tolerated before an error.
  int failure_tolerance = 0;

  // Maximum size of the ratchet window.
  int ratchet_window_size = 0;
};

// Represents a single ICE server configuration.
// Mirrors python: RtcConfiguration.ice_servers[*]
struct IceServer {
  // TURN/STUN server URL (e.g. "stun:stun.l.google.com:19302").
  std::string url;

  // Optional username for TURN authentication.
  std::string username;

  // Optional credential (password) for TURN authentication.
  std::string credential;
};

// WebRTC configuration (ICE, transport, etc.).
// Mirrors python: `RtcConfiguration`
struct RtcConfig {
  // ICE transport type (e.g., ALL, RELAY). Maps to proto::IceTransportType.
  int ice_transport_type = 0;

  // Continuous or single ICE gathering. Maps to
  // proto::ContinualGatheringPolicy.
  int continual_gathering_policy = 0;

  // List of STUN/TURN servers for ICE candidate generation.
  std::vector<IceServer> ice_servers;
};

// Top-level room connection options.
// Mirrors python: `RoomOptions`
struct RoomOptions {
  // If true (default), automatically subscribe to all remote tracks.
  // This is CRITICAL. Without auto_subscribe, you will never receive:
  //   - `track_subscribed` events
  //   - remote audio/video frames
  bool auto_subscribe = true;

  // Enable dynacast (server sends optimal layers depending on subscribers).
  bool dynacast = false;

  // Optional end-to-end encryption settings.
  std::optional<E2EEOptions> e2ee;

  // Optional WebRTC configuration (ICE policy, servers, etc.)
  std::optional<RtcConfig> rtc_config;
};

// Represents a LiveKit room session.
// A Room manages:
//   - the connection to the LiveKit server
//   - participant list (local + remote)
//   - track publications
//   - server events forwarded to a RoomDelegate
class Room {
public:
  Room();
  ~Room();

  // Assign a RoomDelegate that receives room lifecycle callbacks.
  //
  // The delegate must remain valid for the lifetime of the Room or until a
  // different delegate is assigned. The Room does not take ownership.
  // Typical usage:
  //     class MyDelegate : public RoomDelegate { ... };
  //     MyDelegate del;
  //     Room room;
  //     room.setDelegate(&del);
  void setDelegate(RoomDelegate *delegate);

  // Connect to a LiveKit room using the given URL and token,  applying the
  // supplied connection options.
  //
  // Parameters:
  //   url      — WebSocket URL of the LiveKit server.
  //   token    — Access token for authentication.
  //   options  — Connection options controlling auto-subscribe,
  //               dynacast, E2EE, and WebRTC configuration.
  // Behavior:
  //   - Registers an FFI event listener *before* sending the connect request.
  //   - Sends a proto::FfiRequest::Connect with the URL, token,
  //     and the provided RoomOptions.
  //   - Blocks until the FFI connect response arrives.
  //   - Initializes local participant and remote participants.
  //   - Emits room/participant/track events to the delegate.
  // IMPORTANT:
  //   RoomOptions defaults auto_subscribe = true.
  //   Without auto_subscribe enabled, remote tracks will NOT be subscribed
  //   automatically, and no remote audio/video will ever arrive.
  bool Connect(const std::string &url, const std::string &token,
               const RoomOptions &options);

  // Accessors

  // Retrieve static metadata about the room.
  // This contains fields such as:
  //   - SID
  //   - room name
  //   - metadata
  //   - participant counts
  //   - creation timestamp
  RoomInfoData room_info() const;

  // Get the local participant.
  //
  // This object represents the current user, including:
  //   - published tracks (audio/video/screen)
  //   - identity, SID, metadata
  //   - publishing/unpublishing operations
  // Return value:
  //   Non-null pointer after successful Connect().
  LocalParticipant *local_participant() const;

  // Look up a remote participant by identity.
  //
  // Parameters:
  //   identity — The participant’s identity string (not SID)
  // Return value:
  //   Pointer to RemoteParticipant if present, otherwise nullptr.
  // RemoteParticipant contains:
  //   - identity/name/metadata
  //   - track publications
  //   - callbacks for track subscribed/unsubscribed, muted/unmuted
  RemoteParticipant *remote_participant(const std::string &identity) const;

private:
  mutable std::mutex lock_;
  bool connected_{false};
  RoomDelegate *delegate_ = nullptr; // Not owned
  RoomInfoData room_info_;
  std::shared_ptr<FfiHandle> room_handle_;
  std::unique_ptr<LocalParticipant> local_participant_;
  std::unordered_map<std::string, std::shared_ptr<RemoteParticipant>>
      remote_participants_;
  ConnectionState connection_state_ = ConnectionState::Disconnected;

  void OnEvent(const proto::FfiEvent &event);
};
} // namespace livekit

#endif /* LIVEKIT_ROOM_H */
