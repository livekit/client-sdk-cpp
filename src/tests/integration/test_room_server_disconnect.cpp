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

// Exercises the server-initiated disconnect paths that SimulateScenario cannot
// reach: reason-specific disconnects (DuplicateIdentity, ParticipantRemoved,
// RoomDeleted), Eos-driven teardown, peer participant disconnects, and the
// client-vs-server disconnect race.
//
// Requires LIVEKIT_URL and LIVEKIT_TOKEN_A. The peer test additionally needs
// LIVEKIT_TOKEN_B. The admin-driven tests (remove participant / delete room)
// shell out to the `lk` CLI and are skipped unless LIVEKIT_API_KEY and
// LIVEKIT_API_SECRET are set (lk reads url/key/secret from the environment).

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace livekit::test {

namespace {

// ---------------------------------------------------------------------------
// JWT helpers: pull the identity ("sub") and room name ("room") out of a join
// token so the admin tests can target the right participant/room regardless of
// how the tokens were minted.
// ---------------------------------------------------------------------------

std::string base64UrlDecode(const std::string& in) {
  static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  int val = 0;
  int bits = 0;
  for (const char c : in) {
    const auto pos = chars.find(c);
    if (pos == std::string::npos) {
      break; // padding or invalid char: stop
    }
    val = (val << 6) | static_cast<int>(pos);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((val >> bits) & 0xFF));
    }
  }
  return out;
}

// Extracts the string value of `"key":"value"` from a JSON blob. Crude, but
// join tokens are machine-minted so the shape is stable.
std::string extractJsonString(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\":\"";
  const auto start = json.find(needle);
  if (start == std::string::npos) {
    return {};
  }
  const auto value_start = start + needle.size();
  const auto end = json.find('"', value_start);
  if (end == std::string::npos) {
    return {};
  }
  return json.substr(value_start, end - value_start);
}

std::string jwtPayload(const std::string& token) {
  const auto first_dot = token.find('.');
  if (first_dot == std::string::npos) {
    return {};
  }
  const auto second_dot = token.find('.', first_dot + 1);
  if (second_dot == std::string::npos) {
    return {};
  }
  return base64UrlDecode(token.substr(first_dot + 1, second_dot - first_dot - 1));
}

// Values get embedded in a shell command; only accept identifier-ish strings.
bool isShellSafe(const std::string& s) {
  if (s.empty()) {
    return false;
  }
  for (const char c : s) {
    const bool ok = (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '-' || c == '_' || c == '.';
    if (!ok) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Delegate that records the disconnect-related lifecycle callbacks.
// ---------------------------------------------------------------------------

class ServerDisconnectTrackingDelegate : public RoomDelegate {
public:
  void onDisconnected(Room&, const DisconnectedEvent& ev) override {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      ++disconnected_count_;
      last_reason_ = ev.reason;
    }
    cv_.notify_all();
  }

  void onRoomEos(Room&, const RoomEosEvent&) override {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      ++eos_count_;
    }
    cv_.notify_all();
  }

  void onParticipantDisconnected(Room&, const ParticipantDisconnectedEvent& ev) override {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      if (ev.participant != nullptr) {
        disconnected_participants_.push_back(ev.participant->identity());
      }
      last_participant_reason_ = ev.reason;
    }
    cv_.notify_all();
  }

  bool waitForDisconnected(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return disconnected_count_ > 0; });
  }

  bool waitForEos(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return eos_count_ > 0; });
  }

  bool waitForParticipantDisconnected(const std::string& identity, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this, &identity] {
      for (const auto& id : disconnected_participants_) {
        if (id == identity) {
          return true;
        }
      }
      return false;
    });
  }

  int disconnectedCount() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    return disconnected_count_;
  }
  DisconnectReason lastReason() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    return last_reason_;
  }
  DisconnectReason lastParticipantReason() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    return last_participant_reason_;
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int disconnected_count_ = 0;
  int eos_count_ = 0;
  std::vector<std::string> disconnected_participants_;
  DisconnectReason last_reason_ = DisconnectReason::Unknown;
  DisconnectReason last_participant_reason_ = DisconnectReason::Unknown;
};

} // namespace

class RoomServerDisconnectTest : public ::testing::Test {
protected:
  void SetUp() override {
    livekit::initialize(livekit::LogLevel::Info);

    const char* url_env = std::getenv("LIVEKIT_URL");
    const char* token_a_env = std::getenv("LIVEKIT_TOKEN_A");
    const char* token_b_env = std::getenv("LIVEKIT_TOKEN_B");
    if (url_env && token_a_env) {
      server_url_ = url_env;
      token_a_ = token_a_env;
      server_available_ = true;
    }
    if (token_b_env) {
      token_b_ = token_b_env;
    }

    const std::string payload = jwtPayload(token_a_);
    identity_a_ = extractJsonString(payload, "sub");
    room_name_ = extractJsonString(payload, "room");
  }

  void TearDown() override { livekit::shutdown(); }

  // Admin actions go through the `lk` CLI, which reads LIVEKIT_URL,
  // LIVEKIT_API_KEY, and LIVEKIT_API_SECRET from the environment.
  bool adminAvailable() const {
    return std::getenv("LIVEKIT_API_KEY") != nullptr && std::getenv("LIVEKIT_API_SECRET") != nullptr;
  }

  static int runLk(const std::string& args) {
    const std::string cmd = "lk " + args;
    std::cout << "[admin] " << cmd << std::endl;
    return std::system(cmd.c_str()); // NOLINT(concurrency-mt-unsafe)
  }

  bool server_available_ = false;
  std::string server_url_;
  std::string token_a_;
  std::string token_b_;
  std::string identity_a_;
  std::string room_name_;
};

// S1 (DuplicateIdentity): joining with an identity that is already connected
// makes the server disconnect the older connection with DUPLICATE_IDENTITY.
// Needs no admin API — just a second connection with the same token.
TEST_F(RoomServerDisconnectTest, DuplicateIdentityDisconnectsFirstConnection) {
  ASSERT_TRUE(server_available_) << "LIVEKIT_URL and LIVEKIT_TOKEN_A not set";

  Room first;
  ServerDisconnectTrackingDelegate first_delegate;
  first.setDelegate(&first_delegate);
  ASSERT_TRUE(first.connect(server_url_, token_a_, RoomOptions())) << "first connect failed";
  ASSERT_EQ(first.connectionState(), ConnectionState::Connected);

  Room second;
  ASSERT_TRUE(second.connect(server_url_, token_a_, RoomOptions())) << "second connect (same identity) failed";

  EXPECT_TRUE(first_delegate.waitForDisconnected(30s))
      << "first connection should be disconnected when the same identity joins again";
  EXPECT_EQ(first_delegate.lastReason(), DisconnectReason::DuplicateIdentity);
  EXPECT_EQ(first.connectionState(), ConnectionState::Disconnected);
  EXPECT_EQ(first_delegate.disconnectedCount(), 1) << "onDisconnected must fire exactly once";

  second.disconnect();
}

// S1 (ParticipantRemoved): a server-side RemoveParticipant call disconnects the
// client with PARTICIPANT_REMOVED. Driven via `lk room participants remove`.
TEST_F(RoomServerDisconnectTest, RemovedParticipantDisconnectsWithReason) {
  ASSERT_TRUE(server_available_) << "LIVEKIT_URL and LIVEKIT_TOKEN_A not set";
  if (!adminAvailable()) {
    GTEST_SKIP() << "LIVEKIT_API_KEY / LIVEKIT_API_SECRET not set; skipping admin-driven test";
  }
  ASSERT_TRUE(isShellSafe(room_name_) && isShellSafe(identity_a_))
      << "could not extract a usable room ('" << room_name_ << "') / identity ('" << identity_a_
      << "') from LIVEKIT_TOKEN_A";

  Room room;
  ServerDisconnectTrackingDelegate delegate;
  room.setDelegate(&delegate);
  ASSERT_TRUE(room.connect(server_url_, token_a_, RoomOptions())) << "connect failed";
  ASSERT_EQ(room.connectionState(), ConnectionState::Connected);

  ASSERT_EQ(runLk("room participants remove --room " + room_name_ + " --identity " + identity_a_ + " --yes"), 0)
      << "lk room participants remove failed";

  EXPECT_TRUE(delegate.waitForDisconnected(30s)) << "onDisconnected should fire after the server removes us";
  EXPECT_EQ(delegate.lastReason(), DisconnectReason::ParticipantRemoved);
  EXPECT_EQ(room.connectionState(), ConnectionState::Disconnected);
  EXPECT_EQ(delegate.disconnectedCount(), 1);
}

// S1 (RoomDeleted): deleting the room server-side disconnects every participant
// with ROOM_DELETED. Driven via `lk room delete`.
TEST_F(RoomServerDisconnectTest, DeletedRoomDisconnectsWithReason) {
  ASSERT_TRUE(server_available_) << "LIVEKIT_URL and LIVEKIT_TOKEN_A not set";
  if (!adminAvailable()) {
    GTEST_SKIP() << "LIVEKIT_API_KEY / LIVEKIT_API_SECRET not set; skipping admin-driven test";
  }
  ASSERT_TRUE(isShellSafe(room_name_)) << "could not extract a usable room name from LIVEKIT_TOKEN_A";

  Room room;
  ServerDisconnectTrackingDelegate delegate;
  room.setDelegate(&delegate);
  ASSERT_TRUE(room.connect(server_url_, token_a_, RoomOptions())) << "connect failed";
  ASSERT_EQ(room.connectionState(), ConnectionState::Connected);

  ASSERT_EQ(runLk("room delete " + room_name_ + " --yes"), 0) << "lk room delete failed";

  EXPECT_TRUE(delegate.waitForDisconnected(30s)) << "onDisconnected should fire after the room is deleted";
  EXPECT_EQ(delegate.lastReason(), DisconnectReason::RoomDeleted);
  EXPECT_EQ(room.connectionState(), ConnectionState::Disconnected);
  EXPECT_EQ(delegate.disconnectedCount(), 1);
}

// S2 (Eos teardown): after a server-initiated disconnect, the Rust layer closes
// the room event stream. The kEos handler performs the actual state teardown
// (participants dropped, handles released) and fires onRoomEos.
TEST_F(RoomServerDisconnectTest, ServerDisconnectTearsDownViaEos) {
  ASSERT_TRUE(server_available_) << "LIVEKIT_URL and LIVEKIT_TOKEN_A not set";

  Room room;
  ServerDisconnectTrackingDelegate delegate;
  room.setDelegate(&delegate);
  ASSERT_TRUE(room.connect(server_url_, token_a_, RoomOptions())) << "connect failed";
  ASSERT_FALSE(room.localParticipant().expired());

  // ServerLeave produces a server-initiated disconnect without admin access.
  ASSERT_TRUE(room.simulateScenario(SimulateScenario::ServerLeave));

  ASSERT_TRUE(delegate.waitForDisconnected(30s)) << "onDisconnected should fire after ServerLeave";
  EXPECT_TRUE(delegate.waitForEos(30s)) << "onRoomEos should fire after a server-initiated disconnect";

  EXPECT_EQ(room.connectionState(), ConnectionState::Disconnected);
  EXPECT_TRUE(room.localParticipant().expired()) << "kEos teardown should release the local participant";
  EXPECT_EQ(delegate.disconnectedCount(), 1) << "the Eos path must not double-fire onDisconnected";
}

// S6 (peer disconnect): when another participant leaves, the remaining client
// gets onParticipantDisconnected with that identity, and the cached remote
// participant handle expires.
TEST_F(RoomServerDisconnectTest, PeerDisconnectFiresParticipantDisconnected) {
  ASSERT_TRUE(server_available_) << "LIVEKIT_URL and LIVEKIT_TOKEN_A not set";
  if (token_b_.empty()) {
    GTEST_SKIP() << "LIVEKIT_TOKEN_B not set; skipping two-participant test";
  }

  Room room;
  ServerDisconnectTrackingDelegate delegate;
  room.setDelegate(&delegate);
  ASSERT_TRUE(room.connect(server_url_, token_a_, RoomOptions())) << "connect failed";

  Room peer;
  ASSERT_TRUE(peer.connect(server_url_, token_b_, RoomOptions())) << "peer connect failed";
  auto peer_local = peer.localParticipant().lock();
  ASSERT_NE(peer_local, nullptr);
  const std::string peer_identity = peer_local->identity();
  peer_local.reset();

  // Wait until the observer sees the peer before disconnecting it.
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (room.remoteParticipant(peer_identity).expired() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(100ms);
  }
  ASSERT_FALSE(room.remoteParticipant(peer_identity).expired()) << "peer never became visible";
  std::weak_ptr<RemoteParticipant> peer_handle = room.remoteParticipant(peer_identity);

  peer.disconnect();

  EXPECT_TRUE(delegate.waitForParticipantDisconnected(peer_identity, 30s))
      << "onParticipantDisconnected should fire when the peer leaves";
  EXPECT_EQ(delegate.lastParticipantReason(), DisconnectReason::ClientInitiated);
  EXPECT_TRUE(peer_handle.expired()) << "cached remote participant handle should expire after the peer leaves";
  EXPECT_EQ(room.connectionState(), ConnectionState::Connected) << "observer must stay connected";

  room.disconnect();
}

// S1×D1 (race): a server-initiated leave racing a client disconnect() must
// resolve to exactly one onDisconnected, whichever side wins.
TEST_F(RoomServerDisconnectTest, ClientDisconnectDuringServerLeaveFiresDisconnectedOnce) {
  ASSERT_TRUE(server_available_) << "LIVEKIT_URL and LIVEKIT_TOKEN_A not set";

  Room room;
  ServerDisconnectTrackingDelegate delegate;
  room.setDelegate(&delegate);
  ASSERT_TRUE(room.connect(server_url_, token_a_, RoomOptions())) << "connect failed";

  // Kick off a server-side leave, then immediately race it with a client
  // disconnect. Which side wins is timing-dependent; the invariants are not.
  ASSERT_TRUE(room.simulateScenario(SimulateScenario::ServerLeave));
  room.disconnect();

  EXPECT_TRUE(delegate.waitForDisconnected(30s)) << "one of the two paths must fire onDisconnected";
  EXPECT_EQ(room.connectionState(), ConnectionState::Disconnected);

  // Give the losing path time to (incorrectly) double-fire before asserting.
  std::this_thread::sleep_for(3s);
  EXPECT_EQ(delegate.disconnectedCount(), 1) << "the two teardown paths must dedupe to exactly one onDisconnected";
}

} // namespace livekit::test
