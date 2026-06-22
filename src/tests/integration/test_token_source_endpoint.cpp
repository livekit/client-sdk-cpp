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
#include <optional>
#include <string>

// Request serialization, response parsing, header passthrough, GET support, and
// sandbox URL/header resolution are covered by mocked unit tests in
// src/tests/unit/test_token_source.cpp. This file holds the end-to-end check
// that requires a real token endpoint plus a running livekit-server.

namespace livekit::test {

namespace {

// Resolve the token-server /createToken URL from the environment.
//
// Primary: TOKEN_SERVER_PORT → http://127.0.0.1:<port>/createToken (matches
// livekit-examples/token-server-node and tests.yml).
//
// Override: LIVEKIT_CREATE_TOKEN_URL supplies a full endpoint URL when the
// server is not on 127.0.0.1 or uses a non-standard path.
std::optional<std::string> resolveCreateTokenUrl() {
  if (const char* url = std::getenv("LIVEKIT_CREATE_TOKEN_URL"); url != nullptr && url[0] != '\0') {
    return std::string(url);
  }
  if (const char* port = std::getenv("TOKEN_SERVER_PORT"); port != nullptr && port[0] != '\0') {
    return std::string("http://127.0.0.1:") + port + "/createToken";
  }
  return std::nullopt;
}

} // namespace

// End-to-end: requires a real token endpoint (token-server-node) pointed at a
// running livekit-server. CI sets TOKEN_SERVER_PORT and starts token-server-node
// with the local dev server's credentials; see tests.yml.
class TokenSourceEndpointConnectTest : public ::testing::Test {
protected:
  void SetUp() override {
    initialize(LogLevel::Info);
    if (const auto url = resolveCreateTokenUrl()) {
      create_token_url_ = *url;
      endpoint_available_ = true;
    }
  }

  void TearDown() override { shutdown(); }

  bool endpoint_available_ = false;
  std::string create_token_url_;
};

TEST_F(TokenSourceEndpointConnectTest, EndpointMintsConnectableToken) {
  if (!endpoint_available_) {
    GTEST_SKIP() << "TOKEN_SERVER_PORT or LIVEKIT_CREATE_TOKEN_URL not set";
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
