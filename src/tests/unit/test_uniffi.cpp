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

#include <livekit_common.hpp>
#include <livekit_datatrack.hpp>
#include <livekit_net.hpp>
#include <livekit_uniffi.hpp>

namespace livekit {
namespace {

TEST(UniFfiBuildInfoTest, ReturnsCrateVersion) {
  const auto version = livekit_uniffi::build_version();
  EXPECT_FALSE(version.empty());
  EXPECT_NE(version, "unknown");
}

TEST(UniFfiLogForwardTest, BootstrapsIdempotently) {
  EXPECT_NO_THROW({
    livekit_uniffi::log_forward_bootstrap(livekit_uniffi::LogForwardFilter::kInfo);
    livekit_uniffi::log_forward_bootstrap(livekit_uniffi::LogForwardFilter::kWarn);
  });
}

TEST(UniFfiGeneratedBindingsTest, ExposesAllGeneratedComponents) {
  SUCCEED() << "All livekit-uniffi generated component headers compiled and linked";
}

} // namespace
} // namespace livekit
