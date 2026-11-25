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

#include <chrono>
#include <cstdlib>
#include <future>
#include <optional>
#include <string>

namespace livekit::test {

using api::AgentDispatchClient;
using api::AgentDispatchClientOptions;
using api::AgentDispatchError;

namespace {

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
  _putenv_s(name, value != nullptr ? value : "");
#else
  if (value != nullptr) {
    setenv(name, value, 1);
  } else {
    unsetenv(name);
  }
#endif
}

// Restores an environment variable on scope exit.
class ScopedEnv {
public:
  ScopedEnv(const char* name, const char* value) : name_(name) {
    if (const char* old = std::getenv(name)) {
      old_ = old;
    }
    setEnv(name, value);
  }
  ~ScopedEnv() { setEnv(name_, old_ ? old_->c_str() : nullptr); }
  ScopedEnv(const ScopedEnv&) = delete;
  ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
  const char* name_;
  std::optional<std::string> old_;
};

} // namespace

TEST(AgentDispatchClientUnitTest, FromEnvWithoutKeysIsInvalidEnvironment) {
  ScopedEnv key("LIVEKIT_API_KEY", nullptr);
  ScopedEnv secret("LIVEKIT_API_SECRET", nullptr);

  auto client = AgentDispatchClient::fromEnv("http://127.0.0.1:1");
  ASSERT_FALSE(client.ok());
  EXPECT_EQ(client.error().kind, AgentDispatchError::Kind::InvalidEnvironment);
}

TEST(AgentDispatchClientUnitTest, UnreachableHostIsRequestError) {
  AgentDispatchClientOptions options;
  options.request_timeout = std::chrono::milliseconds(2000);
  options.failover = false;
  // Port 1 on loopback is closed, so the connection fails at once.
  AgentDispatchClient client("http://127.0.0.1:1", "key", "secret", options);

  auto future = client.listDispatch("room");
  ASSERT_EQ(future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
  auto result = future.get();
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error().kind, AgentDispatchError::Kind::Request);
  EXPECT_FALSE(result.error().message.empty());
}

TEST(AgentDispatchClientUnitTest, CallOutlivesClient) {
  std::future<Result<std::vector<api::AgentDispatch>, AgentDispatchError>> future;
  {
    AgentDispatchClientOptions options;
    options.failover = false;
    AgentDispatchClient client("http://127.0.0.1:1", "key", "secret", options);
    future = client.listDispatch("room");
  }
  ASSERT_EQ(future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
  EXPECT_FALSE(future.get().ok());
}

} // namespace livekit::test
