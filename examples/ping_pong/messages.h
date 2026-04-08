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

#include <cstdint>

namespace ping_pong {

struct PingMessage {
  std::uint64_t id = 0;
  std::int64_t ts_ns = 0;
};

struct PongMessage {
  std::uint64_t rec_id = 0;
  std::int64_t ts_ns = 0;
};

struct LatencyMetrics {
  std::uint64_t id = 0;
  std::int64_t ping_sent_ts_ns = 0;
  std::int64_t pong_sent_ts_ns = 0;
  std::int64_t ping_received_ts_ns = 0;
  std::int64_t round_trip_time_ns = 0;
  std::int64_t pong_to_ping_time_ns = 0;
  std::int64_t ping_to_pong_and_processing_ns = 0;
  double estimated_one_way_latency_ns = 0.0;
  double round_trip_time_ms = 0.0;
  double pong_to_ping_time_ms = 0.0;
  double ping_to_pong_and_processing_ms = 0.0;
  double estimated_one_way_latency_ms = 0.0;
};

} // namespace ping_pong
