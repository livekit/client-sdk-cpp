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
#include <livekit/token_source.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include "token_source_internal.h"

namespace livekit::test {

TEST(TokenSourceJsonTest, BuildRequestJsonIncludesFields) {
  TokenRequestOptions options;
  options.room_name = "my-room";
  options.participant_identity = "user-1";
  options.participant_attributes["role"] = "host";
  options.agent_name = "assistant";

  const std::string json = buildTokenSourceRequestJson(options);
  EXPECT_NE(json.find("\"room_name\":\"my-room\""), std::string::npos);
  EXPECT_NE(json.find("\"participant_identity\":\"user-1\""), std::string::npos);
  EXPECT_NE(json.find("\"role\":\"host\""), std::string::npos);
  EXPECT_NE(json.find("\"agent_name\":\"assistant\""), std::string::npos);
}

TEST(TokenSourceJsonTest, ParseResponseSnakeCase) {
  const std::string json =
      R"({"server_url":"wss://example.livekit.io","participant_token":"jwt-token","room_name":"room-a"})";

  const auto result = parseTokenSourceResponseJson(json);
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, "wss://example.livekit.io");
  EXPECT_EQ(result.value().participant_token, "jwt-token");
}

TEST(TokenSourceJsonTest, ParseResponseCamelCase) {
  const std::string json =
      R"({"serverUrl":"wss://example.livekit.io","participantToken":"jwt-token","participantName":"Alice"})";

  const auto result = parseTokenSourceResponseJson(json);
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, "wss://example.livekit.io");
  EXPECT_EQ(result.value().participant_token, "jwt-token");
}

TEST(TokenSourceJsonTest, ParseResponseInvalidJsonFails) {
  const auto result = parseTokenSourceResponseJson("this-is-not-json");
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().message, "token server response missing server_url");
}

TEST(TokenSourceJsonTest, ParseResponseMissingParticipantTokenFails) {
  const std::string json = R"({"server_url":"wss://example.livekit.io"})";
  const auto result = parseTokenSourceResponseJson(json);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().message, "token server response missing participant_token");
}

TEST(TokenSourceJwtTest, ValidAndExpiredTokens) {
  const std::string valid_token = "eyJhbGciOiJub25lIn0.eyJleHAiOjk5OTk5OTk5OTk5fQ.";
  const std::string expired_token = "eyJhbGciOiJub25lIn0.eyJleHAiOjF9.";

  EXPECT_TRUE(isParticipantTokenValid(valid_token));
  EXPECT_FALSE(isParticipantTokenValid(expired_token));
}

TEST(TokenSourceJwtTest, UnparseableTokenIsInvalid) { EXPECT_FALSE(isParticipantTokenValid("not-a-jwt")); }

TEST(TokenSourceFactoryTest, LiteralTokenSourceReturnsDetails) {
  const std::string server_url = "wss://example.livekit.io";
  const std::string participant_token = "jwt-token";

  auto source = LiteralTokenSource::fromValue(server_url, participant_token);
  const auto result = source->fetch().get();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, server_url);
  EXPECT_EQ(result.value().participant_token, participant_token);
}

TEST(TokenSourceFactoryTest, CustomTokenSourceReceivesOptions) {
  std::optional<std::string> captured_room;
  auto source = CustomTokenSource::fromCallback([&captured_room](const TokenRequestOptions& options)
                                                    -> std::future<Result<TokenSourceResponse, TokenSourceError>> {
    captured_room = options.room_name;
    TokenSourceResponse details;
    details.server_url = "wss://example.livekit.io";
    details.participant_token = "jwt-token";
    std::promise<Result<TokenSourceResponse, TokenSourceError>> promise;
    promise.set_value(Result<TokenSourceResponse, TokenSourceError>::success(details));
    return promise.get_future();
  });

  TokenRequestOptions request;
  request.room_name = "requested-room";
  const auto result = source->fetch(request).get();
  ASSERT_TRUE(result);
  ASSERT_TRUE(captured_room.has_value());
  EXPECT_EQ(*captured_room, "requested-room");
}

TEST(TokenSourceFactoryTest, CachingTokenSourceReusesValidToken) {
  std::atomic<int> fetch_count{0};
  auto inner = CustomTokenSource::fromCallback(
      [&fetch_count](const TokenRequestOptions&) -> std::future<Result<TokenSourceResponse, TokenSourceError>> {
        ++fetch_count;
        TokenSourceResponse details;
        details.server_url = "wss://example.livekit.io";
        details.participant_token = "eyJhbGciOiJub25lIn0.eyJleHAiOjk5OTk5OTk5OTk5fQ.";
        std::promise<Result<TokenSourceResponse, TokenSourceError>> promise;
        promise.set_value(Result<TokenSourceResponse, TokenSourceError>::success(details));
        return promise.get_future();
      });

  auto cached = CachingTokenSource::wrap(std::move(inner));
  TokenRequestOptions request;
  request.room_name = "room";

  const auto first = cached->fetch(request).get();
  const auto second = cached->fetch(request).get();
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(fetch_count.load(), 1);
}

TEST(TokenSourceFactoryTest, CachingTokenSourceRefetchesWhenForced) {
  std::atomic<int> fetch_count{0};
  auto inner = CustomTokenSource::fromCallback(
      [&fetch_count](const TokenRequestOptions&) -> std::future<Result<TokenSourceResponse, TokenSourceError>> {
        ++fetch_count;
        TokenSourceResponse details;
        details.server_url = "wss://example.livekit.io";
        details.participant_token = "eyJhbGciOiJub25lIn0.eyJleHAiOjk5OTk5OTk5OTk5fQ.";
        std::promise<Result<TokenSourceResponse, TokenSourceError>> promise;
        promise.set_value(Result<TokenSourceResponse, TokenSourceError>::success(details));
        return promise.get_future();
      });

  auto cached = CachingTokenSource::wrap(std::move(inner));
  TokenRequestOptions request;

  (void)cached->fetch(request).get();
  (void)cached->fetch(request, true).get();
  EXPECT_EQ(fetch_count.load(), 2);
}

TEST(TokenSourceFactoryTest, CachingTokenSourceRefetchesWhenOptionsChange) {
  std::atomic<int> fetch_count{0};
  auto inner = CustomTokenSource::fromCallback(
      [&fetch_count](const TokenRequestOptions&) -> std::future<Result<TokenSourceResponse, TokenSourceError>> {
        ++fetch_count;
        TokenSourceResponse details;
        details.server_url = "wss://example.livekit.io";
        details.participant_token = "eyJhbGciOiJub25lIn0.eyJleHAiOjk5OTk5OTk5OTk5fQ.";
        std::promise<Result<TokenSourceResponse, TokenSourceError>> promise;
        promise.set_value(Result<TokenSourceResponse, TokenSourceError>::success(details));
        return promise.get_future();
      });

  auto cached = CachingTokenSource::wrap(std::move(inner));

  TokenRequestOptions first_request;
  first_request.room_name = "room-a";
  TokenRequestOptions second_request;
  second_request.room_name = "room-b";

  (void)cached->fetch(first_request).get();
  (void)cached->fetch(second_request).get();
  EXPECT_EQ(fetch_count.load(), 2);
}

TEST(TokenSourceFactoryTest, CachingTokenSourceRefetchesWhenTokenExpired) {
  std::atomic<int> fetch_count{0};
  auto inner = CustomTokenSource::fromCallback(
      [&fetch_count](const TokenRequestOptions&) -> std::future<Result<TokenSourceResponse, TokenSourceError>> {
        const int count = ++fetch_count;
        TokenSourceResponse details;
        details.server_url = "wss://example.livekit.io";
        details.participant_token =
            (count == 1) ? "eyJhbGciOiJub25lIn0.eyJleHAiOjF9." : "eyJhbGciOiJub25lIn0.eyJleHAiOjk5OTk5OTk5OTk5fQ.";
        std::promise<Result<TokenSourceResponse, TokenSourceError>> promise;
        promise.set_value(Result<TokenSourceResponse, TokenSourceError>::success(details));
        return promise.get_future();
      });

  auto cached = CachingTokenSource::wrap(std::move(inner));
  TokenRequestOptions request;
  request.room_name = "room";

  const auto first = cached->fetch(request).get();
  const auto second = cached->fetch(request).get();

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(fetch_count.load(), 2);
  EXPECT_NE(first.value().participant_token, second.value().participant_token);
}

TEST(TokenSourceFactoryTest, CachingTokenSourceRefetchesWhenTokenUnparseable) {
  std::atomic<int> fetch_count{0};
  auto inner = CustomTokenSource::fromCallback(
      [&fetch_count](const TokenRequestOptions&) -> std::future<Result<TokenSourceResponse, TokenSourceError>> {
        const int count = ++fetch_count;
        TokenSourceResponse details;
        details.server_url = "wss://example.livekit.io";
        details.participant_token = (count == 1) ? "not-a-jwt" : "eyJhbGciOiJub25lIn0.eyJleHAiOjk5OTk5OTk5OTk5fQ.";
        std::promise<Result<TokenSourceResponse, TokenSourceError>> promise;
        promise.set_value(Result<TokenSourceResponse, TokenSourceError>::success(details));
        return promise.get_future();
      });

  auto cached = CachingTokenSource::wrap(std::move(inner));
  TokenRequestOptions request;
  request.room_name = "room";

  const auto first = cached->fetch(request).get();
  const auto second = cached->fetch(request).get();

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(fetch_count.load(), 2);
}

TEST(TokenSourceFactoryTest, CachingTokenSourceSerializesConcurrentFetches) {
  std::atomic<int> fetch_count{0};
  std::atomic<int> concurrent_calls{0};
  std::atomic<int> max_concurrent_calls{0};

  auto inner = CustomTokenSource::fromCallback(
      [&fetch_count, &concurrent_calls, &max_concurrent_calls](
          const TokenRequestOptions&) -> std::future<Result<TokenSourceResponse, TokenSourceError>> {
        ++fetch_count;
        const int active = ++concurrent_calls;
        int observed_max = max_concurrent_calls.load();
        while (active > observed_max && !max_concurrent_calls.compare_exchange_weak(observed_max, active)) {
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        TokenSourceResponse details;
        details.server_url = "wss://example.livekit.io";
        details.participant_token = "eyJhbGciOiJub25lIn0.eyJleHAiOjk5OTk5OTk5OTk5fQ.";
        std::promise<Result<TokenSourceResponse, TokenSourceError>> promise;
        promise.set_value(Result<TokenSourceResponse, TokenSourceError>::success(details));
        --concurrent_calls;
        return promise.get_future();
      });

  auto cached = CachingTokenSource::wrap(std::move(inner));
  TokenRequestOptions request;
  request.room_name = "concurrent-room";

  std::vector<std::thread> threads;
  threads.reserve(4);
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&cached, &request]() { (void)cached->fetch(request).get(); });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(fetch_count.load(), 1);
  EXPECT_EQ(max_concurrent_calls.load(), 1);
}

} // namespace livekit::test
