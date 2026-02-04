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

namespace livekit {
namespace test {

class SDKInitializationTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Each test starts with a fresh SDK state
  }

  void TearDown() override {
    // Ensure SDK is shutdown after each test
    livekit::shutdown();
  }
};

TEST_F(SDKInitializationTest, InitializeWithConsoleLogging) {
  bool result = livekit::initialize(livekit::LogSink::kConsole);
  EXPECT_TRUE(result) << "First initialization should succeed";
}

TEST_F(SDKInitializationTest, InitializeWithCallbackLogging) {
  bool result = livekit::initialize(livekit::LogSink::kCallback);
  EXPECT_TRUE(result) << "Initialization with callback logging should succeed";
}

TEST_F(SDKInitializationTest, DoubleInitializationReturnsFalse) {
  bool first = livekit::initialize(livekit::LogSink::kConsole);
  EXPECT_TRUE(first) << "First initialization should succeed";

  bool second = livekit::initialize(livekit::LogSink::kConsole);
  EXPECT_FALSE(second) << "Second initialization should return false";
}

TEST_F(SDKInitializationTest, ReinitializeAfterShutdown) {
  bool first = livekit::initialize(livekit::LogSink::kConsole);
  EXPECT_TRUE(first) << "First initialization should succeed";

  livekit::shutdown();

  bool second = livekit::initialize(livekit::LogSink::kConsole);
  EXPECT_TRUE(second) << "Re-initialization after shutdown should succeed";
}

TEST_F(SDKInitializationTest, ShutdownWithoutInitialize) {
  // Should not crash
  EXPECT_NO_THROW(livekit::shutdown());
}

TEST_F(SDKInitializationTest, MultipleShutdowns) {
  livekit::initialize(livekit::LogSink::kConsole);

  // Multiple shutdowns should not crash
  EXPECT_NO_THROW(livekit::shutdown());
  EXPECT_NO_THROW(livekit::shutdown());
  EXPECT_NO_THROW(livekit::shutdown());
}

} // namespace test
} // namespace livekit
