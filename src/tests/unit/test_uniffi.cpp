/*
 * Copyright 2026 LiveKit, Inc.
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

#include "uniffi_bindgen_adapter.h"

namespace livekit {
namespace {

TEST(UniFfiTest, BuildVersion) {
  const auto version = uniffiBindgenBuildVersion();

  ASSERT_TRUE(version.has_value()) << "Generated UniFFI binding did not return a build version";

  EXPECT_FALSE(version.value().empty());
  EXPECT_NE(version.value(), "unknown");
}

} // namespace
} // namespace livekit
