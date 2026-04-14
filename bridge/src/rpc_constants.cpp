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

#include "livekit_bridge/rpc_constants.h"

namespace livekit_bridge::rpc::track_control {

const char *const kMethod = "lk.bridge.track-control";
const char *const kActionMute = "mute";
const char *const kActionUnmute = "unmute";
const char kDelimiter = ':';
const char *const kResponseOk = "ok";

std::string formatPayload(const char *action, const std::string &track_name) {
  std::string payload;
  payload.reserve(std::char_traits<char>::length(action) + 1 +
                  track_name.size());
  payload += action;
  payload += kDelimiter;
  payload += track_name;
  return payload;
}

} // namespace livekit_bridge::rpc::track_control
