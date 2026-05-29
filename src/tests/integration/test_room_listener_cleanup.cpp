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

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "../common/test_common.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr auto kListenerCleanupTimeout = 10s;
constexpr auto kDuplicateListenerGracePeriod = 500ms;

class ParticipantConnectedCounter : public RoomDelegate {
public:
  void onParticipantConnected(Room&, const ParticipantConnectedEvent& event) override {
    if (event.participant == nullptr) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ++counts_[event.participant->identity()];
    cv_.notify_all();
  }

  bool waitForCount(const std::string& identity, int count, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [&]() { return countForLocked(identity) >= count; });
  }

  int countFor(const std::string& identity) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return countForLocked(identity);
  }

private:
  int countForLocked(const std::string& identity) const {
    const auto it = counts_.find(identity);
    return it == counts_.end() ? 0 : it->second;
  }

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::map<std::string, int> counts_;
};

void expectSingleParticipantConnectedCallback(ParticipantConnectedCounter& counter, const std::string& identity) {
  ASSERT_TRUE(counter.waitForCount(identity, 1, kListenerCleanupTimeout))
      << "Expected one onParticipantConnected callback for " << identity;

  EXPECT_FALSE(counter.waitForCount(identity, 2, kDuplicateListenerGracePeriod))
      << "Duplicate listener delivered multiple participant callbacks";
}

void expectFailedConnectDoesNotDuplicateParticipantCallbacks(const TestConfig& config, const std::string& failed_url,
                                                             const std::string& failed_token) {
  RoomOptions options;
  options.auto_subscribe = true;

  ParticipantConnectedCounter counter;
  Room observed_room;
  observed_room.setDelegate(&counter);

  EXPECT_FALSE(observed_room.connect(failed_url, failed_token, options)) << "Initial failing connect() should fail";

  ASSERT_TRUE(observed_room.connect(config.url, config.token_a, options)) << "Reconnect after failed connect() failed";
  ASSERT_FALSE(observed_room.localParticipant().expired());

  Room peer_room;
  ASSERT_TRUE(peer_room.connect(config.url, config.token_b, options)) << "Peer failed to connect";
  ASSERT_FALSE(peer_room.localParticipant().expired());
  const std::string peer_identity = peer_room.localParticipant().lock()->identity();
  ASSERT_FALSE(peer_identity.empty());

  expectSingleParticipantConnectedCallback(counter, peer_identity);
}

} // namespace

class RoomListenerCleanupIntegrationTest : public LiveKitTestBase {};

TEST_F(RoomListenerCleanupIntegrationTest, FailedInvalidTokenConnectDoesNotLeaveDuplicateListener) {
  if (!config_.available) {
    throw std::runtime_error("LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B not set");
  }

  expectFailedConnectDoesNotDuplicateParticipantCallbacks(config_, config_.url, "invalid_token");
}

TEST_F(RoomListenerCleanupIntegrationTest, FailedInvalidUrlConnectDoesNotLeaveDuplicateListener) {
  if (!config_.available) {
    throw std::runtime_error("LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B not set");
  }

  expectFailedConnectDoesNotDuplicateParticipantCallbacks(config_, "ws://127.0.0.1:9", config_.token_a);
}

TEST_F(RoomListenerCleanupIntegrationTest, AlreadyConnectedConnectDoesNotReplaceOrLeakListener) {
  if (!config_.available) {
    throw std::runtime_error("LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B not set");
  }

  RoomOptions options;
  options.auto_subscribe = true;

  ParticipantConnectedCounter counter;
  Room observed_room;
  observed_room.setDelegate(&counter);

  ASSERT_TRUE(observed_room.connect(config_.url, config_.token_a, options)) << "Initial connect() failed";
  ASSERT_FALSE(observed_room.localParticipant().expired());

  EXPECT_THROW((void)observed_room.connect(config_.url, config_.token_a, options), std::runtime_error);

  Room peer_room;
  ASSERT_TRUE(peer_room.connect(config_.url, config_.token_b, options)) << "Peer failed to connect";
  ASSERT_FALSE(peer_room.localParticipant().expired());
  const std::string peer_identity = peer_room.localParticipant().lock()->identity();
  ASSERT_FALSE(peer_identity.empty());

  expectSingleParticipantConnectedCallback(counter, peer_identity);
}

} // namespace livekit::test
