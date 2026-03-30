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

#pragma once

#include <chrono>

namespace ping_pong {

inline constexpr char kPingParticipantIdentity[] = "ping";
inline constexpr char kPongParticipantIdentity[] = "pong";

inline constexpr char kPingTrackName[] = "ping";
inline constexpr char kPongTrackName[] = "pong";

inline constexpr char kPingIdKey[] = "id";
inline constexpr char kReceivedIdKey[] = "rec_id";
inline constexpr char kTimestampKey[] = "ts";

inline constexpr auto kPingPeriod = std::chrono::seconds(1);
inline constexpr auto kPollPeriod = std::chrono::milliseconds(50);

} // namespace ping_pong
