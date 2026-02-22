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

#include "json_utils.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace simple_robot {

std::string joystick_to_json(const JoystickCommand &cmd) {
  nlohmann::json j;
  j["x"] = cmd.x;
  j["y"] = cmd.y;
  j["z"] = cmd.z;
  return j.dump();
}

JoystickCommand json_to_joystick(const std::string &json) {
  try {
    auto j = nlohmann::json::parse(json);
    JoystickCommand cmd;
    cmd.x = j.at("x").get<double>();
    cmd.y = j.at("y").get<double>();
    cmd.z = j.at("z").get<double>();
    return cmd;
  } catch (const nlohmann::json::exception &e) {
    throw std::runtime_error(std::string("Failed to parse joystick JSON: ") +
                             e.what());
  }
}

} // namespace simple_robot
