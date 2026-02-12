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

/// @file test_rpc_manager.cpp
/// @brief Unit tests for RpcManager.

#include <gtest/gtest.h>

#include "livekit_bridge/rpc_constants.h"
#include "rpc_manager.h"

#include "livekit/local_participant.h"
#include "livekit/rpc_error.h"

#include <string>
#include <vector>

namespace livekit_bridge {
namespace test {

// Records (action, track_name) pairs passed to the TrackActionFn callback.
struct TrackActionRecord {
  std::string action;
  std::string track_name;
};

class RpcManagerTest : public ::testing::Test {
protected:
  std::vector<TrackActionRecord> recorded_actions_;

  std::unique_ptr<RpcManager> makeManager() {
    return std::make_unique<RpcManager>(
        [this](const std::string &action, const std::string &track_name) {
          recorded_actions_.push_back({action, track_name});
        });
  }

  std::unique_ptr<RpcManager> makeThrowingManager() {
    return std::make_unique<RpcManager>([](const std::string &,
                                           const std::string &track_name) {
      throw livekit::RpcError(livekit::RpcError::ErrorCode::APPLICATION_ERROR,
                              "track not found: " + track_name);
    });
  }

  // Helper: call the private handleTrackControlRpc with a given payload.
  std::optional<std::string>
  callHandler(RpcManager &mgr, const std::string &payload,
              const std::string &caller = "test-caller") {
    livekit::RpcInvocationData data;
    data.request_id = "test-request-id";
    data.caller_identity = caller;
    data.payload = payload;
    data.response_timeout_sec = 10.0;
    return mgr.handleTrackControlRpc(data);
  }
};

// ============================================================================
// Construction & lifecycle
// ============================================================================

TEST_F(RpcManagerTest, InitiallyDisabled) {
  auto mgr = makeManager();
  EXPECT_FALSE(mgr->isEnabled());
}

TEST_F(RpcManagerTest, DisableOnAlreadyDisabledIsNoOp) {
  auto mgr = makeManager();
  EXPECT_NO_THROW(mgr->disable());
  EXPECT_FALSE(mgr->isEnabled());
}

TEST_F(RpcManagerTest, DisableMultipleTimesIsIdempotent) {
  auto mgr = makeManager();
  EXPECT_NO_THROW({
    mgr->disable();
    mgr->disable();
    mgr->disable();
  });
}

TEST_F(RpcManagerTest, DestructorWithoutEnableIsSafe) {
  EXPECT_NO_THROW({ auto mgr = makeManager(); });
}

// ============================================================================
// handleTrackControlRpc — payload parsing
// ============================================================================

TEST_F(RpcManagerTest, ValidMutePayload) {
  auto mgr = makeManager();
  auto result = callHandler(*mgr, "mute:my-track");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), rpc::track_control::kResponseOk);

  ASSERT_EQ(recorded_actions_.size(), 1u);
  EXPECT_EQ(recorded_actions_[0].action, "mute");
  EXPECT_EQ(recorded_actions_[0].track_name, "my-track");
}

TEST_F(RpcManagerTest, ValidUnmutePayload) {
  auto mgr = makeManager();
  auto result = callHandler(*mgr, "unmute:cam");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), rpc::track_control::kResponseOk);

  ASSERT_EQ(recorded_actions_.size(), 1u);
  EXPECT_EQ(recorded_actions_[0].action, "unmute");
  EXPECT_EQ(recorded_actions_[0].track_name, "cam");
}

TEST_F(RpcManagerTest, TrackNameWithColons) {
  auto mgr = makeManager();
  auto result = callHandler(*mgr, "mute:track:with:colons");

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(recorded_actions_.size(), 1u);
  EXPECT_EQ(recorded_actions_[0].action, "mute");
  EXPECT_EQ(recorded_actions_[0].track_name, "track:with:colons");
}

TEST_F(RpcManagerTest, TrackNameWithSpaces) {
  auto mgr = makeManager();
  auto result = callHandler(*mgr, "unmute:my track name");

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(recorded_actions_.size(), 1u);
  EXPECT_EQ(recorded_actions_[0].action, "unmute");
  EXPECT_EQ(recorded_actions_[0].track_name, "my track name");
}

// ============================================================================
// handleTrackControlRpc — invalid payloads
// ============================================================================

TEST_F(RpcManagerTest, EmptyPayloadThrows) {
  auto mgr = makeManager();
  EXPECT_THROW(callHandler(*mgr, ""), livekit::RpcError);
  EXPECT_TRUE(recorded_actions_.empty());
}

TEST_F(RpcManagerTest, NoDelimiterThrows) {
  auto mgr = makeManager();
  EXPECT_THROW(callHandler(*mgr, "mutetrack"), livekit::RpcError);
  EXPECT_TRUE(recorded_actions_.empty());
}

TEST_F(RpcManagerTest, LeadingDelimiterThrows) {
  auto mgr = makeManager();
  EXPECT_THROW(callHandler(*mgr, ":track"), livekit::RpcError);
  EXPECT_TRUE(recorded_actions_.empty());
}

TEST_F(RpcManagerTest, UnknownActionThrows) {
  auto mgr = makeManager();
  EXPECT_THROW(callHandler(*mgr, "pause:cam"), livekit::RpcError);
  EXPECT_TRUE(recorded_actions_.empty());
}

TEST_F(RpcManagerTest, CaseSensitiveAction) {
  auto mgr = makeManager();
  EXPECT_THROW(callHandler(*mgr, "MUTE:cam"), livekit::RpcError);
  EXPECT_THROW(callHandler(*mgr, "Mute:cam"), livekit::RpcError);
  EXPECT_TRUE(recorded_actions_.empty());
}

// ============================================================================
// handleTrackControlRpc — TrackActionFn propagation
// ============================================================================

TEST_F(RpcManagerTest, TrackActionFnExceptionPropagates) {
  auto mgr = makeThrowingManager();

  try {
    callHandler(*mgr, "mute:nonexistent");
    FAIL() << "Expected RpcError to propagate from TrackActionFn";
  } catch (const livekit::RpcError &e) {
    EXPECT_EQ(e.code(), static_cast<int>(
                            livekit::RpcError::ErrorCode::APPLICATION_ERROR));
    EXPECT_NE(std::string(e.message()).find("nonexistent"), std::string::npos)
        << "Error message should contain the track name";
  }
}

TEST_F(RpcManagerTest, MultipleCallsAccumulate) {
  auto mgr = makeManager();

  callHandler(*mgr, "mute:audio");
  callHandler(*mgr, "unmute:audio");
  callHandler(*mgr, "mute:video");

  ASSERT_EQ(recorded_actions_.size(), 3u);
  EXPECT_EQ(recorded_actions_[0].action, "mute");
  EXPECT_EQ(recorded_actions_[0].track_name, "audio");
  EXPECT_EQ(recorded_actions_[1].action, "unmute");
  EXPECT_EQ(recorded_actions_[1].track_name, "audio");
  EXPECT_EQ(recorded_actions_[2].action, "mute");
  EXPECT_EQ(recorded_actions_[2].track_name, "video");
}

// ============================================================================
// handleTrackControlRpc — caller identity forwarded
// ============================================================================

TEST_F(RpcManagerTest, CallerIdentityPassedThrough) {
  auto mgr = makeManager();
  livekit::RpcInvocationData data;
  data.request_id = "req-1";
  data.caller_identity = "remote-robot";
  data.payload = "mute:mic";
  data.response_timeout_sec = 5.0;

  auto result = mgr->handleTrackControlRpc(data);

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(recorded_actions_.size(), 1u);
  EXPECT_EQ(recorded_actions_[0].action, "mute");
  EXPECT_EQ(recorded_actions_[0].track_name, "mic");
}

// ============================================================================
// rpc_constants — formatPayload
// ============================================================================

TEST_F(RpcManagerTest, FormatPayloadMute) {
  namespace tc = rpc::track_control;
  std::string payload = tc::formatPayload(tc::kActionMute, "cam");
  EXPECT_EQ(payload, "mute:cam");
}

TEST_F(RpcManagerTest, FormatPayloadUnmute) {
  namespace tc = rpc::track_control;
  std::string payload = tc::formatPayload(tc::kActionUnmute, "mic");
  EXPECT_EQ(payload, "unmute:mic");
}

TEST_F(RpcManagerTest, FormatPayloadEmptyTrackName) {
  namespace tc = rpc::track_control;
  std::string payload = tc::formatPayload(tc::kActionMute, "");
  EXPECT_EQ(payload, "mute:");
}

TEST_F(RpcManagerTest, FormatPayloadRoundTrip) {
  namespace tc = rpc::track_control;
  std::string track_name = "some-track-123";
  std::string payload = tc::formatPayload(tc::kActionMute, track_name);

  auto mgr = makeManager();
  auto result = callHandler(*mgr, payload);

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(recorded_actions_.size(), 1u);
  EXPECT_EQ(recorded_actions_[0].action, tc::kActionMute);
  EXPECT_EQ(recorded_actions_[0].track_name, track_name);
}

} // namespace test
} // namespace livekit_bridge
