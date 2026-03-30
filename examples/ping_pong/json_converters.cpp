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

#include "json_converters.h"

#include "constants.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace ping_pong {

std::string pingMessageToJson(const PingMessage &message) {
  nlohmann::json json;
  json[kPingIdKey] = message.id;
  json[kTimestampKey] = message.ts_ns;
  return json.dump();
}

PingMessage pingMessageFromJson(const std::string &json_text) {
  try {
    const auto json = nlohmann::json::parse(json_text);

    PingMessage message;
    message.id = json.at(kPingIdKey).get<std::uint64_t>();
    message.ts_ns = json.at(kTimestampKey).get<std::int64_t>();
    return message;
  } catch (const nlohmann::json::exception &e) {
    throw std::runtime_error(std::string("Failed to parse ping JSON: ") +
                             e.what());
  }
}

std::string pongMessageToJson(const PongMessage &message) {
  nlohmann::json json;
  json[kReceivedIdKey] = message.rec_id;
  json[kTimestampKey] = message.ts_ns;
  return json.dump();
}

PongMessage pongMessageFromJson(const std::string &json_text) {
  try {
    const auto json = nlohmann::json::parse(json_text);

    PongMessage message;
    message.rec_id = json.at(kReceivedIdKey).get<std::uint64_t>();
    message.ts_ns = json.at(kTimestampKey).get<std::int64_t>();
    return message;
  } catch (const nlohmann::json::exception &e) {
    throw std::runtime_error(std::string("Failed to parse pong JSON: ") +
                             e.what());
  }
}

} // namespace ping_pong
