/*
 * Copyright 2026 LiveKit
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

/// @file rpc_manager.h
/// @brief Internal RPC manager that owns all RPC concerns for the bridge.

#pragma once

#include "livekit/local_participant.h"

#include <cassert>
#include <functional>
#include <optional>
#include <string>

namespace livekit {
struct RpcInvocationData;
} // namespace livekit

namespace livekit_bridge {

namespace test {
class RpcManagerTest;
} // namespace test

/**
 * Owns all RPC concerns for the LiveKitBridge: built-in handler registration
 * and dispatch, user-registered custom handlers, and outgoing RPC calls.
 *
 * The manager is bound to a LocalParticipant via enable() and unbound via
 * disable(). All public methods require the manager to be enabled (i.e.,
 * enable() has been called and disable() has not).
 *
 * Built-in handlers (e.g., track-control) are automatically registered on
 * enable() and unregistered on disable(). User-registered handlers are
 * forwarded directly to the underlying LocalParticipant.
 *
 * Not part of the public API; lives in bridge/src/.
 */
class RpcManager {
public:
  /// Callback the bridge provides to execute a track action
  /// (mute/unmute/release). Throws livekit::RpcError if the track is not found
  /// or the action is invalid.
  using TrackActionFn = std::function<void(const std::string &action,
                                           const std::string &track_name)>;

  explicit RpcManager(TrackActionFn track_action_fn);

  /// Bind to a LocalParticipant and register all built-in RPC handlers.
  /// @pre @p lp must be non-null and remain valid until disable() is called.
  void enable(livekit::LocalParticipant *lp);

  /// Unregister built-in handlers and unbind from the LocalParticipant.
  void disable();

  /// Whether the manager is currently bound to a LocalParticipant.
  bool isEnabled() const { return lp_ != nullptr; }

  // -- Generic RPC --

  /// @brief Perform an RPC call to a remote participant.
  /// @param destination_identity  Identity of the destination participant.
  /// @param method                Name of the RPC method to invoke.
  /// @param payload               Request payload to send to the remote
  /// handler.
  /// @param response_timeout      Optional timeout in seconds for receiving
  ///                             a response. If not set, the server default
  ///                             timeout (15 seconds) is used.
  /// @return The response payload returned by the remote handler.
  /// @throws if the LocalParticipant performRpc fails.
  std::string performRpc(const std::string &destination_identity,
                         const std::string &method, const std::string &payload,
                         const std::optional<double> &response_timeout);

  // -- User-registered handlers --
  /// @brief Register a handler for an incoming RPC method.
  /// @param method_name  Name of the RPC method to handle.
  /// @param handler      Callback to execute when an invocation is received.
  ///                     The handler may return an optional response payload
  ///                     or throw an RpcError to signal failure.
  /// @throws if the LocalParticipant registerRpcMethod fails.
  void registerRpcMethod(const std::string &method_name,
                         livekit::LocalParticipant::RpcHandler handler);

  /// @brief Unregister a handler for an incoming RPC method.
  /// @param method_name  Name of the RPC method to unregister.
  /// @throws if the LocalParticipant unregisterRpcMethod fails.
  void unregisterRpcMethod(const std::string &method_name);

  // -- Built-in outgoing convenience (track control) --

  /// @brief Request a remote participant to mute a published track.
  /// @param destination_identity  Identity of the remote participant.
  /// @param track_name            Name of the track to mute.
  /// @throws if the LocalParticipant requestTrackMute fails.
  void requestTrackMute(const std::string &destination_identity,
                        const std::string &track_name);
  /// @brief Request a remote participant to unmute a published track.
  /// @param destination_identity  Identity of the remote participant.
  /// @param track_name            Name of the track to unmute.
  /// @throws if the LocalParticipant requestTrackUnmute fails.
  void requestTrackUnmute(const std::string &destination_identity,
                          const std::string &track_name);

private:
  friend class test::RpcManagerTest;

  /// @brief Enable built-in handlers.
  /// @throws if the LocalParticipant registerRpcMethod fails.
  void enableBuiltInHandlers();

  /// @brief Disable built-in handlers.
  /// @throws if the LocalParticipant unregisterRpcMethod fails.
  void disableBuiltInHandlers();

  /// @brief Handle a track control RPC.
  /// @param data  The RPC invocation data.
  /// @return The response payload returned by the remote handler.
  /// @throws if the RPC is invalid or the track is not found.
  std::optional<std::string>
  handleTrackControlRpc(const livekit::RpcInvocationData &data);

  /// Callback to execute a track action RPC
  TrackActionFn track_action_fn_;

  /// The LocalParticipant bound to the manager.
  livekit::LocalParticipant *lp_ = nullptr;
};

} // namespace livekit_bridge
