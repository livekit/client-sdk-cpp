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

/// @file rpc_controller.cpp
/// @brief Implementation of RpcController.

#include "rpc_controller.h"
#include "session_manager/rpc_constants.h"

#include "livekit/local_participant.h"
#include "livekit/rpc_error.h"

#include <cassert>
#include <iostream>

namespace session_manager {

RpcController::RpcController(TrackActionFn track_action_fn)
    : track_action_fn_(std::move(track_action_fn)), lp_(nullptr) {}

void RpcController::enable(livekit::LocalParticipant *lp) {
  assert(lp != nullptr);
  lp_ = lp;
  enableBuiltInHandlers();
}

void RpcController::disable() {
  if (lp_) {
    disableBuiltInHandlers();
  }
  lp_ = nullptr;
}

// ---------------------------------------------------------------
// Generic RPC
// ---------------------------------------------------------------

std::string
RpcController::performRpc(const std::string &destination_identity,
                          const std::string &method, const std::string &payload,
                          const std::optional<double> &response_timeout) {
  assert(lp_ != nullptr);
  return lp_->performRpc(destination_identity, method, payload,
                         response_timeout);
}

// ---------------------------------------------------------------
// User-registered handlers
// ---------------------------------------------------------------

void RpcController::registerRpcMethod(
    const std::string &method_name,
    livekit::LocalParticipant::RpcHandler handler) {
  assert(lp_ != nullptr);
  lp_->registerRpcMethod(method_name, std::move(handler));
}

void RpcController::unregisterRpcMethod(const std::string &method_name) {
  assert(lp_ != nullptr);
  lp_->unregisterRpcMethod(method_name);
}

// ---------------------------------------------------------------
// Built-in outgoing convenience (track control)
// ---------------------------------------------------------------

void RpcController::requestRemoteTrackMute(
    const std::string &destination_identity, const std::string &track_name) {
  namespace tc = rpc::track_control;
  performRpc(destination_identity, tc::kMethod,
             tc::formatPayload(tc::kActionMute, track_name), std::nullopt);
}

void RpcController::requestRemoteTrackUnmute(
    const std::string &destination_identity, const std::string &track_name) {
  namespace tc = rpc::track_control;
  performRpc(destination_identity, tc::kMethod,
             tc::formatPayload(tc::kActionUnmute, track_name), std::nullopt);
}

// ---------------------------------------------------------------
// Built-in handler registration
// ---------------------------------------------------------------

void RpcController::enableBuiltInHandlers() {
  assert(lp_ != nullptr);
  lp_->registerRpcMethod(rpc::track_control::kMethod,
                         [this](const livekit::RpcInvocationData &data)
                             -> std::optional<std::string> {
                           return handleTrackControlRpc(data);
                         });
}

void RpcController::disableBuiltInHandlers() {
  assert(lp_ != nullptr);
  lp_->unregisterRpcMethod(rpc::track_control::kMethod);
}

// ---------------------------------------------------------------
// Built-in handler: track control
// ---------------------------------------------------------------

std::optional<std::string>
RpcController::handleTrackControlRpc(const livekit::RpcInvocationData &data) {
  namespace tc = rpc::track_control;

  std::cout << "[RpcController] Handling track control RPC: " << data.payload
            << "\n";
  auto delim = data.payload.find(tc::kDelimiter);
  if (delim == std::string::npos || delim == 0) {
    throw livekit::RpcError(
        livekit::RpcError::ErrorCode::APPLICATION_ERROR,
        "invalid payload format, expected \"<action>:<track_name>\"");
  }
  const std::string action = data.payload.substr(0, delim);
  const std::string track_name = data.payload.substr(delim + 1);

  if (action != tc::kActionMute && action != tc::kActionUnmute) {
    throw livekit::RpcError(livekit::RpcError::ErrorCode::APPLICATION_ERROR,
                            "unknown action: " + action);
  }

  const auto action_enum = action == tc::kActionMute
                               ? rpc::track_control::Action::kActionMute
                               : rpc::track_control::Action::kActionUnmute;

  track_action_fn_(action_enum, track_name);
  return tc::kResponseOk;
}

} // namespace session_manager
