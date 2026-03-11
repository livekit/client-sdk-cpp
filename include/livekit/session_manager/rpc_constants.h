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

/// @file rpc_constants.h
/// @brief Constants for built-in SessionManager RPC methods.

#pragma once

#include <string>

namespace livekit {
namespace rpc {

/// Built-in RPC method name used by remote track control.
/// Allows remote participants to mute or unmute tracks
/// published by this bridge. Must be called after connect().
/// Audio/video tracks support mute and unmute. Data tracks
/// only support mute and unmute.
namespace track_control {

enum class Action { kActionMute, kActionUnmute };

/// RPC method name registered by the SessionManager for remote track control.
constexpr const char *kMethod = "lk.session_manager.track-control";

/// Payload action strings.
constexpr const char *kActionMute = "mute";
constexpr const char *kActionUnmute = "unmute";

/// Delimiter between action and track name in the payload (e.g. "mute:cam").
constexpr char kDelimiter = ':';

/// Response payload returned on success.
constexpr const char *kResponseOk = "ok";

/// Build a track-control RPC payload: "<action>:<track_name>".
inline std::string formatPayload(const char *action,
                                 const std::string &track_name) {
  std::string payload;
  payload.reserve(std::char_traits<char>::length(action) + 1 +
                  track_name.size());
  payload += action;
  payload += kDelimiter;
  payload += track_name;
  return payload;
}

} // namespace track_control
} // namespace rpc
} // namespace livekit
