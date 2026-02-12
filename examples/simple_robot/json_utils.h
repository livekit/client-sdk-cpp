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

#pragma once

#include <string>

namespace simple_robot {

/// Represents a joystick command with three axes.
struct JoystickCommand {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

/// Serialize a JoystickCommand to a JSON string.
/// Example output: {"x":1.0,"y":2.0,"z":3.0}
std::string joystick_to_json(const JoystickCommand &cmd);

/// Deserialize a JSON string into a JoystickCommand.
/// Throws std::runtime_error if the JSON is invalid or missing fields.
JoystickCommand json_to_joystick(const std::string &json);

} // namespace simple_robot
