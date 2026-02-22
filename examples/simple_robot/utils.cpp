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

#include "utils.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace simple_robot {

bool parseArgs(int argc, char *argv[], std::string &url, std::string &token) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      return false;
    }
  }

  auto get_flag_value = [&](const std::string &name, int &i) -> std::string {
    std::string arg = argv[i];
    const std::string eq = name + "=";
    if (arg.rfind(name, 0) == 0) {
      if (arg.size() > name.size() && arg[name.size()] == '=') {
        return arg.substr(eq.size());
      } else if (i + 1 < argc) {
        return std::string(argv[++i]);
      }
    }
    return {};
  };

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a.rfind("--url", 0) == 0) {
      auto v = get_flag_value("--url", i);
      if (!v.empty())
        url = v;
    } else if (a.rfind("--token", 0) == 0) {
      auto v = get_flag_value("--token", i);
      if (!v.empty())
        token = v;
    }
  }

  // Positional args: <url> <token>
  std::vector<std::string> pos;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--", 0) == 0)
      continue;
    pos.push_back(std::move(a));
  }
  if (url.empty() && pos.size() >= 1)
    url = pos[0];
  if (token.empty() && pos.size() >= 2)
    token = pos[1];

  // Environment variable fallbacks
  if (url.empty()) {
    const char *e = std::getenv("LIVEKIT_URL");
    if (e)
      url = e;
  }
  if (token.empty()) {
    const char *e = std::getenv("LIVEKIT_TOKEN");
    if (e)
      token = e;
  }

  return !(url.empty() || token.empty());
}

} // namespace simple_robot
