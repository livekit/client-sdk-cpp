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
 * See the License for the specific language governing permissions and limitations.
 */

#include <gtest/gtest.h>
#include <livekit/livekit.h>
#include <livekit/token_source.h>

#include <cstdlib>
#include <string>

namespace livekit::test {

namespace {

TokenRequestOptions exampleFetchOptions() {
  TokenRequestOptions options;
  options.room_name = "room name";
  options.participant_name = "participant name";
  options.participant_identity = "participant identity";
  options.participant_metadata = R"({"example": "metadata here"})";
  options.agent_name = "agent name";
  options.agent_metadata = R"({"example": "agent metadata here"})";
  return options;
}

std::string fixtureUrl(const char* path) {
  const char* base = std::getenv("LIVEKIT_TOKEN_FIXTURE_URL");
  if (base == nullptr || base[0] == '\0') {
    return {};
  }
  std::string url = base;
  if (!url.empty() && url.back() == '/') {
    url.pop_back();
  }
  return url + path;
}

} // namespace

class TokenSourceEndpointTest : public ::testing::Test {
protected:
  void SetUp() override {
    const char* base = std::getenv("LIVEKIT_TOKEN_FIXTURE_URL");
    fixture_available_ = (base != nullptr && base[0] != '\0');
  }

  bool fixture_available_ = false;
};

TEST_F(TokenSourceEndpointTest, EndpointHappyPathWithAllOptions) {
  if (!fixture_available_) {
    GTEST_SKIP() << "LIVEKIT_TOKEN_FIXTURE_URL not set";
  }

  auto source = EndpointTokenSource::fromUrl(fixtureUrl("/snake"));
  const auto result = source->fetch(exampleFetchOptions()).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, "wss://fixture.livekit.test");
  EXPECT_FALSE(result.value().participant_token.empty());
}

TEST_F(TokenSourceEndpointTest, EndpointHappyPathWithNoOptions) {
  if (!fixture_available_) {
    GTEST_SKIP() << "LIVEKIT_TOKEN_FIXTURE_URL not set";
  }

  auto source = EndpointTokenSource::fromUrl(fixtureUrl("/snake"));
  const auto result = source->fetch({}).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, "wss://fixture.livekit.test");
}

TEST_F(TokenSourceEndpointTest, EndpointFailsOnNon2xx) {
  if (!fixture_available_) {
    GTEST_SKIP() << "LIVEKIT_TOKEN_FIXTURE_URL not set";
  }

  auto source = EndpointTokenSource::fromUrl(fixtureUrl("/forbidden"));
  const auto result = source->fetch(exampleFetchOptions()).get();
  ASSERT_FALSE(result);
  EXPECT_NE(result.error().message.find("403"), std::string::npos);
}

TEST_F(TokenSourceEndpointTest, EndpointMergesCustomHeaders) {
  if (!fixture_available_) {
    GTEST_SKIP() << "LIVEKIT_TOKEN_FIXTURE_URL not set";
  }

  TokenEndpointOptions endpoint_options;
  endpoint_options.headers["Authorization"] = "Bearer my-token";
  endpoint_options.headers["X-Custom"] = "value";

  auto source = EndpointTokenSource::fromUrl(fixtureUrl("/headers"), std::move(endpoint_options));
  const auto result = source->fetch(exampleFetchOptions()).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, "wss://fixture.livekit.test");
}

TEST_F(TokenSourceEndpointTest, EndpointSendsOnlyProvidedFields) {
  if (!fixture_available_) {
    GTEST_SKIP() << "LIVEKIT_TOKEN_FIXTURE_URL not set";
  }

  TokenRequestOptions options;
  options.room_name = "my-room";

  auto source = EndpointTokenSource::fromUrl(fixtureUrl("/snake"));
  const auto result = source->fetch(options).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, "wss://fixture.livekit.test");
  EXPECT_FALSE(result.value().participant_token.empty());
}

TEST_F(TokenSourceEndpointTest, EndpointParsesCamelCaseResponse) {
  if (!fixture_available_) {
    GTEST_SKIP() << "LIVEKIT_TOKEN_FIXTURE_URL not set";
  }

  auto source = EndpointTokenSource::fromUrl(fixtureUrl("/camel"));
  const auto result = source->fetch({}).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, "wss://fixture.livekit.test");
  EXPECT_FALSE(result.value().participant_token.empty());
}

TEST_F(TokenSourceEndpointTest, EndpointFailsOnMalformedJsonResponse) {
  if (!fixture_available_) {
    GTEST_SKIP() << "LIVEKIT_TOKEN_FIXTURE_URL not set";
  }

  auto source = EndpointTokenSource::fromUrl(fixtureUrl("/malformed"));
  const auto result = source->fetch({}).get();
  ASSERT_FALSE(result);
}

TEST_F(TokenSourceEndpointTest, EndpointSupportsGetMethod) {
  if (!fixture_available_) {
    GTEST_SKIP() << "LIVEKIT_TOKEN_FIXTURE_URL not set";
  }

  TokenEndpointOptions endpoint_options;
  endpoint_options.method = "GET";

  auto source = EndpointTokenSource::fromUrl(fixtureUrl("/get-only"), std::move(endpoint_options));
  const auto result = source->fetch({}).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, "wss://fixture.livekit.test");
}

TEST_F(TokenSourceEndpointTest, SandboxTrimsIdAndUsesBaseUrl) {
  if (!fixture_available_) {
    GTEST_SKIP() << "LIVEKIT_TOKEN_FIXTURE_URL not set";
  }

  const char* base = std::getenv("LIVEKIT_TOKEN_FIXTURE_URL");
  ASSERT_NE(base, nullptr);

  std::string base_url = base;
  while (!base_url.empty() && base_url.back() == '/') {
    base_url.pop_back();
  }

  auto source = SandboxTokenSource::fromSandboxId("  sandbox-123  ", {}, base_url);
  const auto result = source->fetch({}).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, "wss://fixture.livekit.test");
}

// End-to-end: requires a real token endpoint (token-server-node) pointed at a
// running livekit-server, advertised via LIVEKIT_CREATE_TOKEN_URL. Unlike the
// fixture tests above, the minted JWT must actually connect to a room. CI starts
// token-server-node with the local dev server's credentials; see tests.yml.
class TokenSourceEndpointConnectTest : public ::testing::Test {
protected:
  void SetUp() override {
    initialize(LogLevel::Info);
    const char* url = std::getenv("LIVEKIT_CREATE_TOKEN_URL");
    if (url != nullptr && url[0] != '\0') {
      create_token_url_ = url;
      endpoint_available_ = true;
    }
  }

  void TearDown() override { shutdown(); }

  bool endpoint_available_ = false;
  std::string create_token_url_;
};

TEST_F(TokenSourceEndpointConnectTest, EndpointMintsConnectableToken) {
  if (!endpoint_available_) {
    GTEST_SKIP() << "LIVEKIT_CREATE_TOKEN_URL not set";
  }

  auto source = EndpointTokenSource::fromUrl(create_token_url_);

  TokenRequestOptions request;
  request.room_name = "cpp_endpoint_e2e";
  request.participant_identity = "cpp-endpoint-e2e";

  Room room;
  RoomOptions options;
  ASSERT_TRUE(room.connect(*source, request, options)) << "endpoint-minted token should connect";
  EXPECT_FALSE(room.localParticipant().expired());
  EXPECT_EQ(room.connectionState(), ConnectionState::Connected);
  EXPECT_TRUE(room.disconnect());
}

} // namespace livekit::test
