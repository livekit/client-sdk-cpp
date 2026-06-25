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
 * See the License for the License governing permissions and limitations.
 */

#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "livekit/result.h"
#include "livekit/visibility.h"

namespace livekit {

/// @brief Credentials produced by a token source and consumed by @ref Room::connect.
///
/// This is an output type: it is what a @ref TokenSourceFixed or
/// @ref TokenSourceConfigurable returns from @c fetch. Applications typically read
/// it rather than construct it. For static credentials, prefer
/// @ref LiteralTokenSource::fromValue, which takes the server URL and token
/// directly instead of requiring you to populate this struct.
///
/// Mirrors the @c livekit.TokenSourceResponse protocol message: only the server
/// URL and participant token are carried; any additional fields a token server
/// returns are ignored.
struct TokenSourceResponse {
  /// WebSocket URL of the LiveKit server.
  std::string server_url;

  /// JWT access token for the participant.
  std::string participant_token;
};

/// @brief Per-call options sent to configurable token sources (endpoint, sandbox, custom).
///
/// All fields are optional. Unset or empty values are omitted from the token-server
/// request body. The token server embeds the provided values into the returned JWT;
/// @ref Room::connect does not read these options directly after fetch — the room,
/// identity, and grants come from the token.
///
/// @note Which fields are honored depends on the token server. The LiveKit Cloud
/// sandbox token server auto-generates @c room_name, @c participant_identity, and
/// related fields when they are omitted. A project token endpoint typically accepts
/// the full set below, including agent dispatch via @c room_config.
struct TokenRequestOptions {
  /// Target room name encoded into the token request.
  ///
  /// Set this when you need a stable room across reconnects or when coordinating
  /// multiple clients in the same session. If omitted, many token servers (including
  /// the sandbox) assign a new room name on each fetch, so repeat connections may
  /// land in different rooms.
  std::optional<std::string> room_name;

  /// Participant display name shown in UIs and room rosters.
  ///
  /// Optional cosmetic label. Does not need to match @c participant_identity.
  /// If omitted, the token server may generate one or leave it unset.
  std::optional<std::string> participant_name;

  /// Stable participant identity encoded into the JWT.
  ///
  /// Set this when the same logical user or device should reconnect with the same
  /// identity (for example, @c "robot-a" in tests). If omitted, many token servers
  /// assign a new identity on each fetch.
  std::optional<std::string> participant_identity;

  /// Opaque participant metadata string stored on the participant record.
  ///
  /// Often JSON. Passed through to the token server for inclusion in the JWT.
  /// Optional unless your backend or agents depend on it.
  std::optional<std::string> participant_metadata;

  /// Key/value participant attributes encoded into the token request.
  ///
  /// Optional. Empty keys are omitted when serializing the request. Attribute
  /// semantics are defined by your token server and application.
  std::map<std::string, std::string> participant_attributes;

  /// Name of a registered LiveKit agent to dispatch into the room.
  ///
  /// When set (alone or with @c agent_metadata / @c agent_deployment), the SDK
  /// sends @c room_config.agents in the token request so the token server can
  /// embed agent dispatch in the JWT. The named agent must already be deployed
  /// and registered with the same @c agent_name; this does not run agent logic
  /// in the client.
  ///
  /// @see https://docs.livekit.io/agents/server/agent-dispatch/
  std::optional<std::string> agent_name;

  /// Opaque metadata passed to the dispatched agent job at startup.
  ///
  /// Often JSON. Applies to the remote agent worker, not the local participant
  /// (use @c participant_metadata for that). Ignored unless @c agent_name is set
  /// or another agent field triggers @c room_config serialization.
  std::optional<std::string> agent_metadata;

  /// LiveKit Cloud deployment to target for agent dispatch.
  ///
  /// Optional. When omitted or empty, the production deployment is used.
  /// Only relevant when dispatching a named agent on LiveKit Cloud.
  std::optional<std::string> agent_deployment;
};

/// @brief HTTP options for @ref EndpointTokenSource.
struct TokenEndpointOptions {
  /// HTTP method (default @c POST).
  std::string method = "POST";

  /// Additional request headers.
  std::map<std::string, std::string> headers;

  /// Request timeout (default 30 seconds).
  std::chrono::milliseconds timeout = std::chrono::seconds(30);
};

/// @brief Error returned when token fetching fails.
struct TokenSourceError {
  std::string message;
};

/// @brief Base interface for token sources that provide full credentials directly.
class LIVEKIT_API TokenSourceFixed {
public:
  virtual ~TokenSourceFixed() = default;

  /// Fetch connection credentials.
  ///
  /// @return Future resolving to connection details or an error.
  virtual std::future<Result<TokenSourceResponse, TokenSourceError>> fetch() = 0;
};

/// @brief Base interface for token sources that generate credentials from request options.
class LIVEKIT_API TokenSourceConfigurable {
public:
  virtual ~TokenSourceConfigurable() = default;

  /// Fetch connection credentials.
  ///
  /// @param options Connection parameters encoded into the token request.
  /// @return Future resolving to connection details or an error.
  virtual std::future<Result<TokenSourceResponse, TokenSourceError>> fetch(const TokenRequestOptions& options = {}) = 0;
};

/// @brief Token source that returns credentials you already created yourself.
///
/// Choose this when your app manually handles token creation/retrieval and you
/// want the SDK to consume those credentials as-is ("literal" workflow). This
/// class is ideal for quick prototypes, tests, and custom flows where you do not
/// want the SDK to issue token-generation requests.
class LIVEKIT_API LiteralTokenSource final : public TokenSourceFixed {
public:
  /// @brief Create a token source from a static server URL and participant token.
  ///
  /// Each @ref fetch call returns the same credentials.
  ///
  /// @param server_url WebSocket URL of the LiveKit server.
  /// @param participant_token JWT access token for the participant.
  static std::unique_ptr<LiteralTokenSource> fromValue(std::string server_url, std::string participant_token);

  /// @brief Create a token source from an async provider that returns full credentials.
  ///
  /// Use this overload when credentials are produced outside the SDK but fetched
  /// lazily (for example, from your own cache or secure storage).
  static std::unique_ptr<LiteralTokenSource> fromProvider(
      std::function<std::future<Result<TokenSourceResponse, TokenSourceError>>()> provider);

  std::future<Result<TokenSourceResponse, TokenSourceError>> fetch() override;

private:
  explicit LiteralTokenSource(TokenSourceResponse details);
  explicit LiteralTokenSource(std::function<std::future<Result<TokenSourceResponse, TokenSourceError>>()> provider);

  TokenSourceResponse details_;
  std::function<std::future<Result<TokenSourceResponse, TokenSourceError>>()> provider_;
};

/// @brief Token source that delegates token generation to your callback.
///
/// Choose this when you already have an internal auth/token system and want to
/// integrate it with LiveKit's request options without adopting the standardized
/// token endpoint format.
class LIVEKIT_API CustomTokenSource final : public TokenSourceConfigurable {
public:
  /// @brief Create a token source that delegates fetching to @p provider.
  ///
  /// The callback receives @ref TokenRequestOptions for each fetch and returns
  /// @ref TokenSourceResponse produced by your application.
  static std::unique_ptr<CustomTokenSource> fromCallback(
      std::function<std::future<Result<TokenSourceResponse, TokenSourceError>>(const TokenRequestOptions&)> provider);

  /// @note This source holds no cache and invokes the provider fresh on every
  /// call. Wrap it in @ref CachingTokenSource to reuse credentials.
  std::future<Result<TokenSourceResponse, TokenSourceError>> fetch(const TokenRequestOptions& options) override;

private:
  explicit CustomTokenSource(
      std::function<std::future<Result<TokenSourceResponse, TokenSourceError>>(const TokenRequestOptions&)> provider);

  std::function<std::future<Result<TokenSourceResponse, TokenSourceError>>(const TokenRequestOptions&)> provider_;
};

/// @brief Token source that calls your backend token endpoint over HTTP.
///
/// Recommended for most production apps: keep API keys server-side, expose a
/// standardized token endpoint, and let the SDK request credentials with room,
/// participant, and agent options.
///
/// @see https://docs.livekit.io/frontends/build/authentication/endpoint/
class LIVEKIT_API EndpointTokenSource final : public TokenSourceConfigurable {
public:
  /// @brief Create a token source that fetches credentials from @p endpoint_url.
  ///
  /// @param endpoint_url URL of your backend token endpoint.
  /// @param options HTTP transport options (method, headers, timeout).
  static std::unique_ptr<EndpointTokenSource> fromUrl(std::string endpoint_url, TokenEndpointOptions options = {});

  /// @note Every fetch issues a fresh HTTP request. Wrap it in
  /// @ref CachingTokenSource to reuse credentials between calls.
  std::future<Result<TokenSourceResponse, TokenSourceError>> fetch(const TokenRequestOptions& options) override;

private:
  // Network transport seam. Mirrors the internal HTTP client signature
  // (returns the raw response body or an error string) so tests can inject a
  // stub and assert the serialized request / parse a canned response without a
  // live server. Defaults to the real HTTP client in production.
  using HttpTransport = std::function<Result<std::string, std::string>(
      const std::string& method, const std::string& url, const std::map<std::string, std::string>& headers,
      const std::string& json_body, std::chrono::milliseconds timeout)>;

  EndpointTokenSource(std::string endpoint_url, TokenEndpointOptions options, HttpTransport transport);

  Result<TokenSourceResponse, TokenSourceError> fetchSync(const TokenRequestOptions& options) const;

  std::string endpoint_url_;
  TokenEndpointOptions options_;
  HttpTransport transport_;

  friend struct EndpointTokenSourceTestAccess;
};

/// @brief Token source that uses LiveKit Cloud's sandbox token server (development only).
///
/// Use this for local development and quick testing when you do not yet have your
/// own backend token endpoint. Do not use in production.
///
/// @see https://docs.livekit.io/frontends/build/authentication/sandbox-token-server/
class LIVEKIT_API SandboxTokenSource final : public TokenSourceConfigurable {
public:
  /// @brief Create a token source backed by the LiveKit Cloud sandbox token server.
  ///
  /// @param sandbox_id Sandbox identifier from LiveKit Cloud (surrounding whitespace is trimmed).
  /// @param options HTTP options (method, headers, timeout).
  /// @param base_url LiveKit Cloud API base URL (default @c https://cloud-api.livekit.io).
  static std::unique_ptr<SandboxTokenSource> fromSandboxId(
      const std::string& sandbox_id, TokenEndpointOptions options = {},
      const std::string& base_url = "https://cloud-api.livekit.io");

  std::future<Result<TokenSourceResponse, TokenSourceError>> fetch(const TokenRequestOptions& options) override;

private:
  SandboxTokenSource(const std::string& sandbox_id, TokenEndpointOptions options, const std::string& base_url);

  std::unique_ptr<TokenSourceConfigurable> endpoint_;

  friend struct SandboxTokenSourceTestAccess;
};

/// @brief Decorator that adds JWT-aware caching to another configurable token source.
///
/// Wrap @ref CustomTokenSource, @ref EndpointTokenSource, or
/// @ref SandboxTokenSource to reduce token fetch calls. A cached response is
/// reused until the request options change or the JWT expires. Call
/// @ref invalidate to force the next @ref fetch to bypass the cache.
class LIVEKIT_API CachingTokenSource final : public TokenSourceConfigurable {
public:
  /// @brief Wrap @p inner with JWT-aware caching.
  ///
  /// Cached values are keyed by @ref TokenRequestOptions.
  static std::unique_ptr<CachingTokenSource> wrap(std::unique_ptr<TokenSourceConfigurable> inner);

  std::future<Result<TokenSourceResponse, TokenSourceError>> fetch(const TokenRequestOptions& options) override;

  /// @brief Clear any cached credentials so the next @ref fetch re-queries the inner source.
  void invalidate();

  /// @brief Return the currently cached credentials, if any.
  ///
  /// @return The cached response, or @c std::nullopt when nothing is cached.
  std::optional<TokenSourceResponse> cachedResponse() const;

private:
  explicit CachingTokenSource(std::unique_ptr<TokenSourceConfigurable> inner);

  std::unique_ptr<TokenSourceConfigurable> inner_;
  mutable std::mutex mutex_;
  std::optional<TokenRequestOptions> cached_options_;
  std::optional<TokenSourceResponse> cached_details_;
};

} // namespace livekit
