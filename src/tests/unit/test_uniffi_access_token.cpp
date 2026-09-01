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

#include <iostream>
#include <livekit_uniffi.hpp>

namespace livekit {
namespace {

TEST(UniFfiAccessTokenTest, GeneratesAndVerifiesParticipantToken) {
  livekit_uniffi::VideoGrants grants{};
  grants.room_join = true;
  grants.room = "hello-world";

  livekit_uniffi::TokenOptions options{};
  options.identity = "cpp-participant";
  options.name = "C++ Participant";
  options.video_grants = grants;

  const livekit_uniffi::ApiCredentials credentials{"devkey", "secret"};
  const auto token = livekit_uniffi::token_generate(options, credentials);

  ASSERT_FALSE(token.empty());

  const auto claims = livekit_uniffi::token_verify(token, credentials);
  std::cout << "Generated UniFFI access token: " << token << '\n'
            << "Verified claims: issuer=" << claims.iss << ", identity=" << claims.sub << ", room=" << claims.video.room
            << '\n';

  EXPECT_EQ(claims.iss, credentials.key);
  EXPECT_EQ(claims.sub, *options.identity);
  EXPECT_EQ(claims.name, *options.name);
  EXPECT_TRUE(claims.video.room_join);
  EXPECT_EQ(claims.video.room, grants.room);
}

} // namespace
} // namespace livekit
