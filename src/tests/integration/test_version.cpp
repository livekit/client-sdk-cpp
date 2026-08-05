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

#include <gtest/gtest.h>
#include <livekit/livekit.h>
#include <livekit/version.h>

#include <cctype>
#include <string>

#if !defined(LIVEKIT_CLIENT_SDK_VERSION_MAJOR)
#error "LIVEKIT_CLIENT_SDK_VERSION_MAJOR must be defined"
#endif
#if !defined(LIVEKIT_CLIENT_SDK_VERSION_MINOR)
#error "LIVEKIT_CLIENT_SDK_VERSION_MINOR must be defined"
#endif
#if !defined(LIVEKIT_CLIENT_SDK_VERSION_PATCH)
#error "LIVEKIT_CLIENT_SDK_VERSION_PATCH must be defined"
#endif
#if !defined(LIVEKIT_CLIENT_SDK_VERSION)
#error "LIVEKIT_CLIENT_SDK_VERSION must be defined"
#endif

namespace livekit::test {
namespace {

bool isNonNegativeIntegerToken(const std::string& token) {
  if (token.empty()) {
    return false;
  }
  for (char c : token) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

} // namespace

TEST(VersionHeaderTest, MacrosExistAndMatchSemverFormat) {
  EXPECT_GE(LIVEKIT_CLIENT_SDK_VERSION_MAJOR, 0);
  EXPECT_GE(LIVEKIT_CLIENT_SDK_VERSION_MINOR, 0);
  EXPECT_GE(LIVEKIT_CLIENT_SDK_VERSION_PATCH, 0);

  const std::string version = LIVEKIT_CLIENT_SDK_VERSION;
  ASSERT_FALSE(version.empty());

  const std::string expected_core = std::to_string(LIVEKIT_CLIENT_SDK_VERSION_MAJOR) + "." +
                                    std::to_string(LIVEKIT_CLIENT_SDK_VERSION_MINOR) + "." +
                                    std::to_string(LIVEKIT_CLIENT_SDK_VERSION_PATCH);

  ASSERT_GE(version.size(), expected_core.size());
  EXPECT_EQ(version.compare(0, expected_core.size(), expected_core), 0)
      << "LIVEKIT_CLIENT_SDK_VERSION='" << version << "' must start with MAJOR.MINOR.PATCH '" << expected_core << "'";

  if (version.size() > expected_core.size()) {
    const char separator = version[expected_core.size()];
    EXPECT_TRUE(separator == '-' || separator == '+')
        << "semver core must be followed by '-' (pre-release) or '+' (build), got '" << separator << "' in '" << version
        << "'";
  }

  EXPECT_TRUE(isNonNegativeIntegerToken(std::to_string(LIVEKIT_CLIENT_SDK_VERSION_MAJOR)));
  EXPECT_TRUE(isNonNegativeIntegerToken(std::to_string(LIVEKIT_CLIENT_SDK_VERSION_MINOR)));
  EXPECT_TRUE(isNonNegativeIntegerToken(std::to_string(LIVEKIT_CLIENT_SDK_VERSION_PATCH)));
}

} // namespace livekit::test
