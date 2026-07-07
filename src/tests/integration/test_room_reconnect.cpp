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

// Exercises the server-initiated reconnect / leave paths end-to-end by
// injecting SimulateScenario over the live signalling connection. These are the
// paths (onReconnecting / onReconnected / server-driven onDisconnected) that no
// other client API can reach without server-admin calls.
//
// Requires LIVEKIT_URL and LIVEKIT_TOKEN_A (same as the other integration tests).

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <string>

using namespace std::chrono_literals;

namespace livekit::test {

namespace {

// Records reconnect lifecycle callbacks and lets a test block until each fires.
class ReconnectTrackingDelegate : public RoomDelegate {
public:
  void onReconnecting(Room&, const ReconnectingEvent&) override {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      ++reconnecting_count_;
    }
    cv_.notify_all();
  }

  void onReconnected(Room&, const ReconnectedEvent&) override {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      ++reconnected_count_;
    }
    cv_.notify_all();
  }

  void onDisconnected(Room&, const DisconnectedEvent& ev) override {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      ++disconnected_count_;
      last_reason_ = ev.reason;
    }
    cv_.notify_all();
  }

  bool waitForReconnecting(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return reconnecting_count_ > 0; });
  }

  bool waitForReconnected(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return reconnected_count_ > 0; });
  }

  bool waitForDisconnected(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return disconnected_count_ > 0; });
  }

  int reconnectingCount() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    return reconnecting_count_;
  }
  int reconnectedCount() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    return reconnected_count_;
  }
  int disconnectedCount() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    return disconnected_count_;
  }
  DisconnectReason lastReason() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    return last_reason_;
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int reconnecting_count_ = 0;
  int reconnected_count_ = 0;
  int disconnected_count_ = 0;
  DisconnectReason last_reason_ = DisconnectReason::Unknown;
};

} // namespace

class RoomReconnectTest : public ::testing::Test {
protected:
  void SetUp() override {
    livekit::initialize(livekit::LogLevel::Info);

    const char* url_env = std::getenv("LIVEKIT_URL");
    const char* token_env = std::getenv("LIVEKIT_TOKEN_A");
    if (url_env && token_env) {
      server_url_ = url_env;
      token_ = token_env;
      server_available_ = true;
    }
  }

  void TearDown() override { livekit::shutdown(); }

  bool server_available_ = false;
  std::string server_url_;
  std::string token_;
};

// SignalReconnect closes the signal channel locally; the engine resumes the
// session, which should surface as onReconnecting followed by onReconnected,
// with the room still Connected afterward.
TEST_F(RoomReconnectTest, SignalReconnectResumesSession) {
  ASSERT_TRUE(server_available_) << "LIVEKIT_URL and LIVEKIT_TOKEN_A not set";

  Room room;
  ReconnectTrackingDelegate delegate;
  room.setDelegate(&delegate);

  ASSERT_TRUE(room.connect(server_url_, token_, RoomOptions())) << "connect failed";
  ASSERT_EQ(room.connectionState(), ConnectionState::Connected);

  ASSERT_TRUE(room.simulateScenario(SimulateScenario::SignalReconnect))
      << "FFI should accept the SignalReconnect scenario";

  EXPECT_TRUE(delegate.waitForReconnecting(15s)) << "onReconnecting should fire after SignalReconnect";
  EXPECT_TRUE(delegate.waitForReconnected(30s)) << "onReconnected should fire once the resume completes";
  EXPECT_EQ(room.connectionState(), ConnectionState::Connected) << "room should be Connected after a successful resume";

  room.disconnect();
}

// FullReconnect forces a full reconnect (new session; local tracks republished).
// It should also surface as onReconnecting followed by onReconnected.
TEST_F(RoomReconnectTest, FullReconnectReestablishesSession) {
  ASSERT_TRUE(server_available_) << "LIVEKIT_URL and LIVEKIT_TOKEN_A not set";

  Room room;
  ReconnectTrackingDelegate delegate;
  room.setDelegate(&delegate);

  ASSERT_TRUE(room.connect(server_url_, token_, RoomOptions())) << "connect failed";
  ASSERT_EQ(room.connectionState(), ConnectionState::Connected);

  ASSERT_TRUE(room.simulateScenario(SimulateScenario::FullReconnect)) << "FFI should accept the FullReconnect scenario";

  EXPECT_TRUE(delegate.waitForReconnecting(15s)) << "onReconnecting should fire after FullReconnect";
  EXPECT_TRUE(delegate.waitForReconnected(30s)) << "onReconnected should fire once the full reconnect completes";
  EXPECT_EQ(room.connectionState(), ConnectionState::Connected);

  room.disconnect();
}

// ServerLeave asks the server to send a (non-reconnect) Leave, which should
// tear the session down and surface as a single server-initiated onDisconnected.
TEST_F(RoomReconnectTest, ServerLeaveDisconnects) {
  ASSERT_TRUE(server_available_) << "LIVEKIT_URL and LIVEKIT_TOKEN_A not set";

  Room room;
  ReconnectTrackingDelegate delegate;
  room.setDelegate(&delegate);

  ASSERT_TRUE(room.connect(server_url_, token_, RoomOptions())) << "connect failed";
  ASSERT_EQ(room.connectionState(), ConnectionState::Connected);

  ASSERT_TRUE(room.simulateScenario(SimulateScenario::ServerLeave)) << "FFI should accept the ServerLeave scenario";

  EXPECT_TRUE(delegate.waitForDisconnected(30s)) << "onDisconnected should fire after a server-initiated leave";
}

} // namespace livekit::test
