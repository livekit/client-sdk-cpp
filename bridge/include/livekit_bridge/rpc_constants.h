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
/// @brief Constants for built-in bridge RPC methods.

#pragma once

#include <string>

#ifdef _WIN32
#ifdef livekit_bridge_EXPORTS
#define LIVEKIT_BRIDGE_API __declspec(dllexport)
#else
#define LIVEKIT_BRIDGE_API __declspec(dllimport)
#endif
#else
#define LIVEKIT_BRIDGE_API
#endif

namespace livekit_bridge {
namespace rpc {

/// Built-in RPC method name used by remote track control.
/// Allows remote participants to mute or unmute tracks
/// published by this bridge. Must be called after connect().
/// Audio/video tracks support mute and unmute. Data tracks
/// only support mute and unmute.
namespace track_control {

enum class Action { kActionMute, kActionUnmute };

/// RPC method name registered by the bridge for remote track control.
LIVEKIT_BRIDGE_API extern const char *const kMethod;

/// Payload action strings.
LIVEKIT_BRIDGE_API extern const char *const kActionMute;
LIVEKIT_BRIDGE_API extern const char *const kActionUnmute;

/// Delimiter between action and track name in the payload (e.g. "mute:cam").
LIVEKIT_BRIDGE_API extern const char kDelimiter;

/// Response payload returned on success.
LIVEKIT_BRIDGE_API extern const char *const kResponseOk;

/// Build a track-control RPC payload: "<action>:<track_name>".
LIVEKIT_BRIDGE_API std::string formatPayload(const char *action,
                                             const std::string &track_name);

} // namespace track_control
} // namespace rpc
} // namespace livekit_bridge
