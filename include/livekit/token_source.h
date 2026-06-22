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

/// @brief Credentials returned by a @ref TokenSourceFixed or @ref TokenSourceConfigurable.
struct ConnectionDetails {
  /// WebSocket URL of the LiveKit server.
  std::string server_url;

  /// JWT access token for the participant.
  std::string participant_token;

  /// Optional participant display name metadata.
  ///
  /// Populate this when your token provider (endpoint/sandbox/custom callback)
  /// returns a canonical display name for UI/session context. This field is not
  /// required for @ref Room::connect; literal/manual-token workflows typically
  /// leave it unset unless the application already has this value.
  std::optional<std::string> participant_name;

  /// Optional room name metadata.
  ///
  /// Populate this when your token provider returns the resolved room name (for
  /// example, server-side room assignment). This field is not required for
  /// @ref Room::connect; literal/manual-token workflows usually leave it unset
  /// unless the application wants to preserve it for app-level logic.
  std::optional<std::string> room_name;
};

/// @brief Per-call options sent to configurable token sources (endpoint, sandbox, custom).
struct TokenRequestOptions {
  std::optional<std::string> room_name;
  std::optional<std::string> participant_name;
  std::optional<std::string> participant_identity;
  std::optional<std::string> participant_metadata;
  std::map<std::string, std::string> participant_attributes;
  std::optional<std::string> agent_name;
  std::optional<std::string> agent_metadata;
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
///
/// Use this shape when token selection does not depend on per-connection request
/// fields (room, participant identity, agent selection, and so on). This is most
/// useful when your app already has complete @ref ConnectionDetails and only needs
/// to hand them to the SDK.
class LIVEKIT_API TokenSourceFixed {
public:
  virtual ~TokenSourceFixed();

  /// Fetch connection credentials.
  ///
  /// @return Future resolving to connection details or an error.
  virtual std::future<Result<ConnectionDetails, TokenSourceError>> fetch() = 0;
};

/// @brief Base interface for token sources that generate credentials from request options.
///
/// Use this shape when token generation depends on room/participant/agent inputs
/// supplied at connection time. Most production integrations use this interface,
/// either via a backend token endpoint, custom callback logic, or the sandbox
/// token server during development.
class LIVEKIT_API TokenSourceConfigurable {
public:
  virtual ~TokenSourceConfigurable();

  /// Fetch connection credentials.
  ///
  /// @param options Connection parameters encoded into the token request.
  /// @param force_refresh When @c true, bypass any cached credentials.
  /// @return Future resolving to connection details or an error.
  virtual std::future<Result<ConnectionDetails, TokenSourceError>> fetch(const TokenRequestOptions& options = {},
                                                                         bool force_refresh = false) = 0;
};

/// @brief Token source that returns credentials you already created yourself.
///
/// Choose this when your app manually handles token creation/retrieval and you
/// want the SDK to consume those credentials as-is ("literal" workflow). This
/// class is ideal for quick prototypes, tests, and custom flows where you do not
/// want the SDK to issue token-generation requests.
class LIVEKIT_API LiteralTokenSource final : public TokenSourceFixed {
public:
  /// @brief Create a token source from static @ref ConnectionDetails.
  ///
  /// Each @ref fetch call returns the same credentials.
  static std::unique_ptr<LiteralTokenSource> fromDetails(ConnectionDetails details);

  /// @brief Create a token source from an async provider that returns full credentials.
  ///
  /// Use this overload when credentials are produced outside the SDK but fetched
  /// lazily (for example, from your own cache or secure storage).
  static std::unique_ptr<LiteralTokenSource> fromProvider(
      std::function<std::future<Result<ConnectionDetails, TokenSourceError>>()> provider);

  std::future<Result<ConnectionDetails, TokenSourceError>> fetch() override;

private:
  explicit LiteralTokenSource(ConnectionDetails details);
  explicit LiteralTokenSource(std::function<std::future<Result<ConnectionDetails, TokenSourceError>>()> provider);

  ConnectionDetails details_;
  std::function<std::future<Result<ConnectionDetails, TokenSourceError>>()> provider_;
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
  /// @ref ConnectionDetails produced by your application.
  static std::unique_ptr<CustomTokenSource> fromCallback(
      std::function<std::future<Result<ConnectionDetails, TokenSourceError>>(const TokenRequestOptions&)> provider);

  std::future<Result<ConnectionDetails, TokenSourceError>> fetch(const TokenRequestOptions& options,
                                                                 bool force_refresh = false) override;

private:
  explicit CustomTokenSource(
      std::function<std::future<Result<ConnectionDetails, TokenSourceError>>(const TokenRequestOptions&)> provider);

  std::function<std::future<Result<ConnectionDetails, TokenSourceError>>(const TokenRequestOptions&)> provider_;
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

  std::future<Result<ConnectionDetails, TokenSourceError>> fetch(const TokenRequestOptions& options,
                                                                 bool force_refresh = false) override;

private:
  EndpointTokenSource(std::string endpoint_url, TokenEndpointOptions options);

  Result<ConnectionDetails, TokenSourceError> fetchSync(const TokenRequestOptions& options) const;

  std::string endpoint_url_;
  TokenEndpointOptions options_;
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

  std::future<Result<ConnectionDetails, TokenSourceError>> fetch(const TokenRequestOptions& options,
                                                                 bool force_refresh = false) override;

private:
  SandboxTokenSource(const std::string& sandbox_id, TokenEndpointOptions options, const std::string& base_url);

  std::unique_ptr<TokenSourceConfigurable> endpoint_;
};

/// @brief Decorator that adds JWT-aware caching to another configurable token source.
///
/// Wrap @ref CustomTokenSource, @ref EndpointTokenSource, or
/// @ref SandboxTokenSource to reduce token fetch calls while still refreshing
/// when tokens expire or when @p force_refresh is requested.
class LIVEKIT_API CachingTokenSource final : public TokenSourceConfigurable {
public:
  /// @brief Wrap @p inner with JWT-aware caching.
  ///
  /// Cached values are keyed by @ref TokenRequestOptions.
  static std::unique_ptr<CachingTokenSource> wrap(std::unique_ptr<TokenSourceConfigurable> inner);

  std::future<Result<ConnectionDetails, TokenSourceError>> fetch(const TokenRequestOptions& options,
                                                                 bool force_refresh = false) override;

private:
  explicit CachingTokenSource(std::unique_ptr<TokenSourceConfigurable> inner);

  std::unique_ptr<TokenSourceConfigurable> inner_;
  mutable std::mutex mutex_;
  std::optional<TokenRequestOptions> cached_options_;
  std::optional<ConnectionDetails> cached_details_;
};

} // namespace livekit
