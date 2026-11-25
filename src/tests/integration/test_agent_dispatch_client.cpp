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
#include <livekit/api/agent_dispatch_client.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <string>

#include "../common/test_common.h"

namespace livekit::test {

using api::AgentDispatchClient;
using api::AgentDispatchError;
using api::CreateAgentDispatchRequest;
using api::JobRestartPolicy;

namespace {

std::string envOr(const char* name, const char* fallback) {
  const char* value = std::getenv(name);
  return value != nullptr && value[0] != '\0' ? value : fallback;
}

struct ScopeExit {
  std::function<void()> fn;
  ~ScopeExit() {
    if (fn) {
      fn();
    }
  }
};

std::string uniqueRoom() {
  return "cpp_agent_dispatch_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

// Requires a running livekit-server. LIVEKIT_URL (ws:// or http://) and the
// API key/secret come from scripts/set-test-tokens.sh. No agent worker is
// needed: the server keeps a dispatch that has no jobs.
class AgentDispatchClientTest : public ::testing::Test {
protected:
  void SetUp() override {
    if (std::getenv("LIVEKIT_URL") == nullptr) {
      if (runningInCi()) {
        FAIL() << "LIVEKIT_URL not set";
      }
      GTEST_SKIP() << "LIVEKIT_URL not set; source scripts/set-test-tokens.sh";
    }
    host_ = envOr("LIVEKIT_URL", "");
    api_key_ = envOr("LIVEKIT_API_KEY", "devkey");
    api_secret_ = envOr("LIVEKIT_API_SECRET", "secret");
  }

  std::string host_;
  std::string api_key_;
  std::string api_secret_;
};

TEST_F(AgentDispatchClientTest, CreateGetListDelete) {
  AgentDispatchClient client(host_, api_key_, api_secret_);
  const std::string room = uniqueRoom();

  CreateAgentDispatchRequest req;
  req.agent_name = "cpp-test-agent";
  req.room = room;
  req.metadata = R"({"k":"v"})";
  req.restart_policy = JobRestartPolicy::Never;
  req.attributes = {{"source", "cpp-test"}};

  auto created = client.createDispatch(req).get();
  ASSERT_TRUE(created.ok()) << created.error().message;
  const auto& dispatch = created.value();
  // Remove the dispatch even when an assertion below fails.
  ScopeExit cleanup{[&] { (void)client.deleteDispatch(dispatch.id, room).get(); }};
  EXPECT_FALSE(dispatch.id.empty());
  EXPECT_EQ(dispatch.agent_name, req.agent_name);
  EXPECT_EQ(dispatch.room, room);
  EXPECT_EQ(dispatch.metadata, req.metadata);
  EXPECT_EQ(dispatch.attributes, req.attributes);

  auto got = client.getDispatch(dispatch.id, room).get();
  ASSERT_TRUE(got.ok()) << got.error().message;
  ASSERT_TRUE(got.value().has_value());
  EXPECT_EQ(got.value()->id, dispatch.id);

  auto listed = client.listDispatch(room).get();
  ASSERT_TRUE(listed.ok()) << listed.error().message;
  // A project with automatic dispatch can add its own dispatches to the room.
  const auto& all = listed.value();
  EXPECT_TRUE(std::any_of(all.begin(), all.end(), [&](const api::AgentDispatch& d) { return d.id == dispatch.id; }));

  cleanup.fn = nullptr;
  auto deleted = client.deleteDispatch(dispatch.id, room).get();
  ASSERT_TRUE(deleted.ok()) << deleted.error().message;
  EXPECT_EQ(deleted.value().id, dispatch.id);

  auto after = client.getDispatch(dispatch.id, room).get();
  ASSERT_TRUE(after.ok()) << after.error().message;
  EXPECT_FALSE(after.value().has_value());
}

TEST_F(AgentDispatchClientTest, WrongSecretIsUnauthenticated) {
  AgentDispatchClient client(host_, api_key_, "wrong-secret-that-is-long-enough-for-hs256");
  auto result = client.listDispatch(uniqueRoom()).get();
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error().kind, AgentDispatchError::Kind::Server);
  EXPECT_EQ(result.error().code, "unauthenticated") << result.error().message;
}

TEST_F(AgentDispatchClientTest, FromEnvUsesApiKeyAndSecret) {
  if (std::getenv("LIVEKIT_API_KEY") == nullptr || std::getenv("LIVEKIT_API_SECRET") == nullptr) {
    GTEST_SKIP() << "LIVEKIT_API_KEY / LIVEKIT_API_SECRET not set";
  }
  auto client = AgentDispatchClient::fromEnv(host_);
  ASSERT_TRUE(client.ok()) << client.error().message;
  // ListDispatch on a room that does not exist fails on the server, so create one first.
  const std::string room = uniqueRoom();
  CreateAgentDispatchRequest req;
  req.agent_name = "cpp-test-agent";
  req.room = room;
  auto created = client.value().createDispatch(req).get();
  ASSERT_TRUE(created.ok()) << created.error().message;
  auto deleted = client.value().deleteDispatch(created.value().id, room).get();
  EXPECT_TRUE(deleted.ok()) << deleted.error().message;
}

} // namespace livekit::test
