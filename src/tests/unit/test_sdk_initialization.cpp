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

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <string>

#include "lk_log.h"

namespace livekit::test {

class SDKInitializationTest : public ::testing::Test {
protected:
  void SetUp() override {}

  void TearDown() override { livekit::shutdown(); }
};

TEST_F(SDKInitializationTest, InitializeDefault) {
  bool result = livekit::initialize();
  EXPECT_TRUE(result) << "First initialization should succeed";
}

TEST_F(SDKInitializationTest, InitializeWithLogLevel) {
  bool result = livekit::initialize(livekit::LogLevel::Debug);
  EXPECT_TRUE(result) << "Initialization with explicit log level should succeed";
  EXPECT_EQ(livekit::getLogLevel(), livekit::LogLevel::Debug);
}

TEST_F(SDKInitializationTest, InitializeWithLogLevelAndCallback) {
  std::size_t callback_count = 0;
  bool result = livekit::initialize(livekit::LogLevel::Debug,
                                    [&callback_count](livekit::LogLevel level, const std::string& logger_name,
                                                      const std::string& message) { callback_count++; });
  EXPECT_TRUE(result) << "Initialization with explicit log level and callback should succeed";
  EXPECT_EQ(livekit::getLogLevel(), livekit::LogLevel::Debug);

  // Simple log to ensure callback wired up right
  LK_LOG_DEBUG("hello from test");
  EXPECT_EQ(callback_count, 1);
}

TEST_F(SDKInitializationTest, DoubleInitializationReturnsFalse) {
  bool first = livekit::initialize();
  EXPECT_TRUE(first) << "First initialization should succeed";

  bool second = livekit::initialize();
  EXPECT_FALSE(second) << "Second initialization should return false";
}

TEST_F(SDKInitializationTest, ReinitializeAfterShutdown) {
  bool first = livekit::initialize();
  EXPECT_TRUE(first) << "First initialization should succeed";

  livekit::shutdown();

  bool second = livekit::initialize();
  EXPECT_TRUE(second) << "Re-initialization after shutdown should succeed";
}

TEST_F(SDKInitializationTest, ShutdownWithoutInitialize) { EXPECT_NO_THROW(livekit::shutdown()); }

TEST_F(SDKInitializationTest, MultipleShutdowns) {
  livekit::initialize();

  EXPECT_NO_THROW(livekit::shutdown());
  EXPECT_NO_THROW(livekit::shutdown());
  EXPECT_NO_THROW(livekit::shutdown());
}

TEST(SDKBuildInfoTest, ServerFacingVersionDoesNotIncludeBuildFlavorSuffix) {
  const std::string version = LIVEKIT_BUILD_VERSION;

  EXPECT_FALSE(version.empty());
  EXPECT_EQ(version.find("-debug"), std::string::npos);
  EXPECT_EQ(version.find("-release"), std::string::npos);
}

} // namespace livekit::test
