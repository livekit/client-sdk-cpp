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
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "token_source_internal.h"

namespace livekit::test {

namespace {

// A non-expired unsigned JWT (alg=none, exp far in the future) used for stubbed
// token-endpoint responses.
constexpr const char* kValidToken = "eyJhbGciOiJub25lIn0.eyJleHAiOjk5OTk5OTk5OTk5fQ.";
constexpr const char* kServerUrl = "wss://localhost:7000";

// Captures the arguments the token source passed to the HTTP transport so tests
// can assert the serialized request (mirrors mocking global fetch in the JS SDK).
struct CapturedRequest {
  std::string method;
  std::string url;
  std::map<std::string, std::string> headers;
  std::string body;
  int calls = 0;
};

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

std::string successResponseJson(const std::string& extra_fields = "") {
  return std::string(R"({"server_url":")") + kServerUrl + R"(","participant_token":")" + kValidToken + "\"" +
         extra_fields + "}";
}

// Builds a transport that records the request into `capture` and returns `response`.
TokenSourceHttpTransport makeStubTransport(const std::shared_ptr<CapturedRequest>& capture,
                                           const Result<std::string, std::string>& response) {
  return [capture, response](const std::string& method, const std::string& url,
                             const std::map<std::string, std::string>& headers, const std::string& body,
                             std::chrono::milliseconds) {
    capture->method = method;
    capture->url = url;
    capture->headers = headers;
    capture->body = body;
    capture->calls += 1;
    return response;
  };
}

} // namespace

TEST(TokenSourceEndpointMockTest, SendsAllProvidedFields) {
  auto capture = std::make_shared<CapturedRequest>();
  auto source = EndpointTokenSourceTestAccess::create(
      "https://example.com/token", {},
      makeStubTransport(capture, Result<std::string, std::string>::success(successResponseJson())));

  const auto result = source->fetch(exampleFetchOptions()).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, kServerUrl);
  EXPECT_EQ(result.value().participant_token, kValidToken);

  EXPECT_EQ(capture->calls, 1);
  EXPECT_EQ(capture->method, "POST");
  EXPECT_EQ(capture->url, "https://example.com/token");
  EXPECT_NE(capture->body.find("\"room_name\":\"room name\""), std::string::npos);
  EXPECT_NE(capture->body.find("\"participant_name\":\"participant name\""), std::string::npos);
  EXPECT_NE(capture->body.find("\"participant_identity\":\"participant identity\""), std::string::npos);
  EXPECT_NE(capture->body.find("\"participant_metadata\":"), std::string::npos);
  // Agent options are packaged into room_config.agents (per the standard endpoint contract).
  EXPECT_NE(capture->body.find("\"room_config\""), std::string::npos);
  EXPECT_NE(capture->body.find("\"agents\""), std::string::npos);
  EXPECT_NE(capture->body.find("\"agent_name\":\"agent name\""), std::string::npos);
}

TEST(TokenSourceEndpointMockTest, SendsEmptyBodyWithNoOptions) {
  auto capture = std::make_shared<CapturedRequest>();
  auto source = EndpointTokenSourceTestAccess::create(
      "https://example.com/token", {},
      makeStubTransport(capture, Result<std::string, std::string>::success(successResponseJson())));

  const auto result = source->fetch({}).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(capture->body, "{}");
}

TEST(TokenSourceEndpointMockTest, SendsOnlyProvidedFields) {
  auto capture = std::make_shared<CapturedRequest>();
  auto source = EndpointTokenSourceTestAccess::create(
      "https://example.com/token", {},
      makeStubTransport(capture, Result<std::string, std::string>::success(successResponseJson())));

  TokenRequestOptions options;
  options.room_name = "my-room";
  const auto result = source->fetch(options).get();
  ASSERT_TRUE(result);
  EXPECT_NE(capture->body.find("\"room_name\":\"my-room\""), std::string::npos);
  // No agent fields were provided, so room_config must be omitted entirely.
  EXPECT_EQ(capture->body.find("room_config"), std::string::npos);
}

TEST(TokenSourceEndpointMockTest, MergesCustomHeaders) {
  auto capture = std::make_shared<CapturedRequest>();
  TokenEndpointOptions endpoint_options;
  endpoint_options.headers["Authorization"] = "Bearer my-token";
  endpoint_options.headers["X-Custom"] = "value";

  auto source = EndpointTokenSourceTestAccess::create(
      "https://example.com/token", std::move(endpoint_options),
      makeStubTransport(capture, Result<std::string, std::string>::success(successResponseJson())));

  const auto result = source->fetch(exampleFetchOptions()).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(capture->headers["Authorization"], "Bearer my-token");
  EXPECT_EQ(capture->headers["X-Custom"], "value");
}

TEST(TokenSourceEndpointMockTest, FailsOnTransportError) {
  auto capture = std::make_shared<CapturedRequest>();
  auto source = EndpointTokenSourceTestAccess::create(
      "https://example.com/token", {},
      makeStubTransport(capture, Result<std::string, std::string>::failure("token server returned status 403")));

  const auto result = source->fetch(exampleFetchOptions()).get();
  ASSERT_FALSE(result);
  EXPECT_NE(result.error().message.find("403"), std::string::npos);
}

TEST(TokenSourceEndpointMockTest, ParsesCamelCaseResponse) {
  auto capture = std::make_shared<CapturedRequest>();
  const std::string camel = std::string(R"({"serverUrl":")") + kServerUrl + R"(","participantToken":")" + kValidToken +
                            R"(","participantName":"Alice"})";
  auto source = EndpointTokenSourceTestAccess::create(
      "https://example.com/token", {}, makeStubTransport(capture, Result<std::string, std::string>::success(camel)));

  const auto result = source->fetch({}).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, kServerUrl);
  EXPECT_EQ(result.value().participant_token, kValidToken);
  ASSERT_TRUE(result.value().participant_name.has_value());
  EXPECT_EQ(*result.value().participant_name, "Alice");
}

TEST(TokenSourceEndpointMockTest, IgnoresUnknownResponseFields) {
  auto capture = std::make_shared<CapturedRequest>();
  auto source = EndpointTokenSourceTestAccess::create(
      "https://example.com/token", {},
      makeStubTransport(capture, Result<std::string, std::string>::success(
                                     successResponseJson(R"(,"some_future_field":"ignored","another_unknown":42)"))));

  const auto result = source->fetch(exampleFetchOptions()).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, kServerUrl);
  EXPECT_EQ(result.value().participant_token, kValidToken);
}

TEST(TokenSourceEndpointMockTest, FailsOnMalformedResponse) {
  auto capture = std::make_shared<CapturedRequest>();
  auto source = EndpointTokenSourceTestAccess::create(
      "https://example.com/token", {},
      makeStubTransport(capture, Result<std::string, std::string>::success("this-is-not-json")));

  const auto result = source->fetch({}).get();
  ASSERT_FALSE(result);
}

TEST(TokenSourceEndpointMockTest, SupportsGetMethod) {
  auto capture = std::make_shared<CapturedRequest>();
  TokenEndpointOptions endpoint_options;
  endpoint_options.method = "GET";
  auto source = EndpointTokenSourceTestAccess::create(
      "https://example.com/token", std::move(endpoint_options),
      makeStubTransport(capture, Result<std::string, std::string>::success(successResponseJson())));

  const auto result = source->fetch({}).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(capture->method, "GET");
}

TEST(TokenSourceSandboxMockTest, SetsSandboxHeaderAndResolvesUrl) {
  auto capture = std::make_shared<CapturedRequest>();
  SandboxTokenServerOptions options;
  options.base_url = "https://cloud-api.livekit.io";
  auto source = SandboxTokenSourceTestAccess::create(
      "  sandbox-123  ", std::move(options),
      makeStubTransport(capture, Result<std::string, std::string>::success(successResponseJson())));

  const auto result = source->fetch({}).get();
  ASSERT_TRUE(result);
  EXPECT_EQ(capture->url, "https://cloud-api.livekit.io/api/v2/sandbox/connection-details");
  EXPECT_EQ(capture->headers["X-Sandbox-ID"], "sandbox-123");
}

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

TEST(TokenSourceJsonTest, ParseResponseSnakeCaseMinimal) {
  const std::string json = R"({"server_url":"wss://example.livekit.io","participant_token":"jwt-token"})";

  const auto result = parseTokenSourceResponseJson(json);
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, "wss://example.livekit.io");
  EXPECT_EQ(result.value().participant_token, "jwt-token");
  EXPECT_FALSE(result.value().room_name.has_value());
  EXPECT_FALSE(result.value().participant_name.has_value());
}

TEST(TokenSourceJsonTest, ParseResponseSnakeCaseFull) {
  const std::string json =
      R"({"server_url":"wss://example.livekit.io","participant_token":"jwt-token","room_name":"room-a","participant_name":"Alice"})";

  const auto result = parseTokenSourceResponseJson(json);
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, "wss://example.livekit.io");
  EXPECT_EQ(result.value().participant_token, "jwt-token");
  ASSERT_TRUE(result.value().room_name.has_value());
  EXPECT_EQ(*result.value().room_name, "room-a");
  ASSERT_TRUE(result.value().participant_name.has_value());
  EXPECT_EQ(*result.value().participant_name, "Alice");
}

TEST(TokenSourceJsonTest, ParseResponseCamelCaseMinimal) {
  const std::string json = R"({"serverUrl":"wss://example.livekit.io","participantToken":"jwt-token"})";

  const auto result = parseTokenSourceResponseJson(json);
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, "wss://example.livekit.io");
  EXPECT_EQ(result.value().participant_token, "jwt-token");
  EXPECT_FALSE(result.value().room_name.has_value());
  EXPECT_FALSE(result.value().participant_name.has_value());
}

TEST(TokenSourceJsonTest, ParseResponseCamelCaseFull) {
  const std::string json =
      R"({"serverUrl":"wss://example.livekit.io","participantToken":"jwt-token","roomName":"room-a","participantName":"Alice"})";

  const auto result = parseTokenSourceResponseJson(json);
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, "wss://example.livekit.io");
  EXPECT_EQ(result.value().participant_token, "jwt-token");
  ASSERT_TRUE(result.value().room_name.has_value());
  EXPECT_EQ(*result.value().room_name, "room-a");
  ASSERT_TRUE(result.value().participant_name.has_value());
  EXPECT_EQ(*result.value().participant_name, "Alice");
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

  auto source = LiteralTokenSource::fromLiteral(server_url, participant_token);
  const auto result = source->fetch().get();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().server_url, server_url);
  EXPECT_EQ(result.value().participant_token, participant_token);
}

TEST(TokenSourceFactoryTest, CustomTokenSourceReceivesOptions) {
  std::optional<std::string> captured_room;
  auto source = CustomTokenSource::fromCustom([&captured_room](const TokenRequestOptions& options)
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
  auto inner = CustomTokenSource::fromCustom(
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

TEST(TokenSourceFactoryTest, CachingTokenSourceRefetchesAfterInvalidate) {
  std::atomic<int> fetch_count{0};
  auto inner = CustomTokenSource::fromCustom(
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
  cached->invalidate();
  (void)cached->fetch(request).get();
  EXPECT_EQ(fetch_count.load(), 2);
}

TEST(TokenSourceFactoryTest, CachingTokenSourceExposesCachedResponse) {
  auto inner = CustomTokenSource::fromCustom(
      [](const TokenRequestOptions&) -> std::future<Result<TokenSourceResponse, TokenSourceError>> {
        TokenSourceResponse details;
        details.server_url = "wss://example.livekit.io";
        details.participant_token = "eyJhbGciOiJub25lIn0.eyJleHAiOjk5OTk5OTk5OTk5fQ.";
        std::promise<Result<TokenSourceResponse, TokenSourceError>> promise;
        promise.set_value(Result<TokenSourceResponse, TokenSourceError>::success(details));
        return promise.get_future();
      });

  auto cached = CachingTokenSource::wrap(std::move(inner));
  TokenRequestOptions request;

  EXPECT_FALSE(cached->cachedResponse().has_value());

  (void)cached->fetch(request).get();
  const auto stored = cached->cachedResponse();
  ASSERT_TRUE(stored.has_value());
  EXPECT_EQ(stored->server_url, "wss://example.livekit.io");

  cached->invalidate();
  EXPECT_FALSE(cached->cachedResponse().has_value());
}

TEST(TokenSourceFactoryTest, CachingTokenSourceRefetchesWhenOptionsChange) {
  std::atomic<int> fetch_count{0};
  auto inner = CustomTokenSource::fromCustom(
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
  auto inner = CustomTokenSource::fromCustom(
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
  auto inner = CustomTokenSource::fromCustom(
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

  auto inner = CustomTokenSource::fromCustom(
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
