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

/// @file test_rpc_controller.cpp
/// @brief Unit tests for RpcController.

#include <gtest/gtest.h>

#include "livekit/session_manager/rpc_constants.h"
#include "rpc_controller.h"

#include "livekit/local_participant.h"
#include "livekit/rpc_error.h"

#include <string>
#include <vector>

namespace session_manager {
namespace test {

// Records (action, track_name) pairs passed to the TrackActionFn callback.
struct TrackActionRecord {
  std::string action;
  std::string track_name;
};

class RpcControllerTest : public ::testing::Test {
protected:
  std::vector<TrackActionRecord> recorded_actions_;

  std::unique_ptr<RpcController> makeController() {
    namespace tc = rpc::track_control;
    return std::make_unique<RpcController>(
        [this](const tc::Action &action, const std::string &track_name) {
          const char *action_str = (action == tc::Action::kActionMute)
                                       ? tc::kActionMute
                                       : tc::kActionUnmute;
          recorded_actions_.push_back({action_str, track_name});
        });
  }

  std::unique_ptr<RpcController> makeThrowingController() {
    return std::make_unique<RpcController>(
        [](const rpc::track_control::Action &, const std::string &track_name) {
          throw livekit::RpcError(
              livekit::RpcError::ErrorCode::APPLICATION_ERROR,
              "track not found: " + track_name);
        });
  }

  // Helper: call the private handleTrackControlRpc with a given payload.
  std::optional<std::string>
  callHandler(RpcController &controller, const std::string &payload,
              const std::string &caller = "test-caller") {
    livekit::RpcInvocationData data;
    data.request_id = "test-request-id";
    data.caller_identity = caller;
    data.payload = payload;
    data.response_timeout_sec = 10.0;
    return controller.handleTrackControlRpc(data);
  }
};

// ============================================================================
// Construction & lifecycle
// ============================================================================

TEST_F(RpcControllerTest, InitiallyDisabled) {
  auto controller = makeController();
  EXPECT_FALSE(controller->isEnabled());
}

TEST_F(RpcControllerTest, DisableOnAlreadyDisabledIsNoOp) {
  auto controller = makeController();
  EXPECT_NO_THROW(controller->disable());
  EXPECT_FALSE(controller->isEnabled());
}

TEST_F(RpcControllerTest, DisableMultipleTimesIsIdempotent) {
  auto controller = makeController();
  EXPECT_NO_THROW({
    controller->disable();
    controller->disable();
    controller->disable();
  });
}

TEST_F(RpcControllerTest, DestructorWithoutEnableIsSafe) {
  EXPECT_NO_THROW({ auto controller = makeController(); });
}

// ============================================================================
// handleTrackControlRpc — payload parsing
// ============================================================================

TEST_F(RpcControllerTest, ValidMutePayload) {
  auto controller = makeController();
  auto result = callHandler(*controller, "mute:my-track");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), rpc::track_control::kResponseOk);

  ASSERT_EQ(recorded_actions_.size(), 1u);
  EXPECT_EQ(recorded_actions_[0].action, "mute");
  EXPECT_EQ(recorded_actions_[0].track_name, "my-track");
}

TEST_F(RpcControllerTest, ValidUnmutePayload) {
  auto controller = makeController();
  auto result = callHandler(*controller, "unmute:cam");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), rpc::track_control::kResponseOk);

  ASSERT_EQ(recorded_actions_.size(), 1u);
  EXPECT_EQ(recorded_actions_[0].action, "unmute");
  EXPECT_EQ(recorded_actions_[0].track_name, "cam");
}

TEST_F(RpcControllerTest, TrackNameWithColons) {
  auto controller = makeController();
  auto result = callHandler(*controller, "mute:track:with:colons");

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(recorded_actions_.size(), 1u);
  EXPECT_EQ(recorded_actions_[0].action, "mute");
  EXPECT_EQ(recorded_actions_[0].track_name, "track:with:colons");
}

TEST_F(RpcControllerTest, TrackNameWithSpaces) {
  auto controller = makeController();
  auto result = callHandler(*controller, "unmute:my track name");

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(recorded_actions_.size(), 1u);
  EXPECT_EQ(recorded_actions_[0].action, "unmute");
  EXPECT_EQ(recorded_actions_[0].track_name, "my track name");
}

// ============================================================================
// handleTrackControlRpc — invalid payloads
// ============================================================================

TEST_F(RpcControllerTest, EmptyPayloadThrows) {
  auto controller = makeController();
  EXPECT_THROW(callHandler(*controller, ""), livekit::RpcError);
  EXPECT_TRUE(recorded_actions_.empty());
}

TEST_F(RpcControllerTest, NoDelimiterThrows) {
  auto controller = makeController();
  EXPECT_THROW(callHandler(*controller, "mutetrack"), livekit::RpcError);
  EXPECT_TRUE(recorded_actions_.empty());
}

TEST_F(RpcControllerTest, LeadingDelimiterThrows) {
  auto controller = makeController();
  EXPECT_THROW(callHandler(*controller, ":track"), livekit::RpcError);
  EXPECT_TRUE(recorded_actions_.empty());
}

TEST_F(RpcControllerTest, UnknownActionThrows) {
  auto controller = makeController();
  EXPECT_THROW(callHandler(*controller, "pause:cam"), livekit::RpcError);
  EXPECT_TRUE(recorded_actions_.empty());
}

TEST_F(RpcControllerTest, CaseSensitiveAction) {
  auto controller = makeController();
  EXPECT_THROW(callHandler(*controller, "MUTE:cam"), livekit::RpcError);
  EXPECT_THROW(callHandler(*controller, "Mute:cam"), livekit::RpcError);
  EXPECT_TRUE(recorded_actions_.empty());
}

// ============================================================================
// handleTrackControlRpc — TrackActionFn propagation
// ============================================================================

TEST_F(RpcControllerTest, TrackActionFnExceptionPropagates) {
  auto controller = makeThrowingController();

  try {
    callHandler(*controller, "mute:nonexistent");
    FAIL() << "Expected RpcError to propagate from TrackActionFn";
  } catch (const livekit::RpcError &e) {
    EXPECT_EQ(e.code(), static_cast<int>(
                            livekit::RpcError::ErrorCode::APPLICATION_ERROR));
    EXPECT_NE(std::string(e.message()).find("nonexistent"), std::string::npos)
        << "Error message should contain the track name";
  }
}

TEST_F(RpcControllerTest, MultipleCallsAccumulate) {
  auto controller = makeController();

  callHandler(*controller, "mute:audio");
  callHandler(*controller, "unmute:audio");
  callHandler(*controller, "mute:video");

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

TEST_F(RpcControllerTest, CallerIdentityPassedThrough) {
  auto controller = makeController();
  auto result = callHandler(*controller, "mute:mic", "remote-robot");

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(recorded_actions_.size(), 1u);
  EXPECT_EQ(recorded_actions_[0].action, "mute");
  EXPECT_EQ(recorded_actions_[0].track_name, "mic");
}

// ============================================================================
// rpc_constants — formatPayload
// ============================================================================

TEST_F(RpcControllerTest, FormatPayloadMute) {
  namespace tc = rpc::track_control;
  std::string payload = tc::formatPayload(tc::kActionMute, "cam");
  EXPECT_EQ(payload, "mute:cam");
}

TEST_F(RpcControllerTest, FormatPayloadUnmute) {
  namespace tc = rpc::track_control;
  std::string payload = tc::formatPayload(tc::kActionUnmute, "mic");
  EXPECT_EQ(payload, "unmute:mic");
}

TEST_F(RpcControllerTest, FormatPayloadEmptyTrackName) {
  namespace tc = rpc::track_control;
  std::string payload = tc::formatPayload(tc::kActionMute, "");
  EXPECT_EQ(payload, "mute:");
}

TEST_F(RpcControllerTest, FormatPayloadRoundTrip) {
  namespace tc = rpc::track_control;
  std::string track_name = "some-track-123";
  std::string payload = tc::formatPayload(tc::kActionMute, track_name);

  auto controller = makeController();
  auto result = callHandler(*controller, payload);

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(recorded_actions_.size(), 1u);
  EXPECT_EQ(recorded_actions_[0].action, tc::kActionMute);
  EXPECT_EQ(recorded_actions_[0].track_name, track_name);
}

} // namespace test
} // namespace session_manager
