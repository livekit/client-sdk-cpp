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

  /// Optional participant display name returned by the token server.
  std::optional<std::string> participant_name;

  /// Optional room name returned by the token server.
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

/// @brief Fixed token source: @ref fetch takes no parameters.
class LIVEKIT_API TokenSourceFixed {
public:
  virtual ~TokenSourceFixed();

  /// Fetch connection credentials.
  ///
  /// @return Future resolving to connection details or an error.
  virtual std::future<Result<ConnectionDetails, TokenSourceError>> fetch() = 0;
};

/// @brief Configurable token source: @ref fetch accepts @ref TokenRequestOptions.
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

/// @brief Fixed token source backed by static connection details or an async provider.
class LIVEKIT_API LiteralTokenSource final : public TokenSourceFixed {
public:
  /// @brief Create a token source from static @ref ConnectionDetails.
  static std::unique_ptr<LiteralTokenSource> fromDetails(ConnectionDetails details);

  /// @brief Create a token source from an async provider (fixed credentials per call).
  static std::unique_ptr<LiteralTokenSource> fromProvider(
      std::function<std::future<Result<ConnectionDetails, TokenSourceError>>()> provider);

  std::future<Result<ConnectionDetails, TokenSourceError>> fetch() override;

private:
  explicit LiteralTokenSource(ConnectionDetails details);
  explicit LiteralTokenSource(std::function<std::future<Result<ConnectionDetails, TokenSourceError>>()> provider);

  ConnectionDetails details_;
  std::function<std::future<Result<ConnectionDetails, TokenSourceError>>()> provider_;
};

/// @brief Configurable token source backed by custom application logic.
class LIVEKIT_API CustomTokenSource final : public TokenSourceConfigurable {
public:
  /// @brief Create a token source that delegates fetching to @p provider.
  static std::unique_ptr<CustomTokenSource> fromCallback(
      std::function<std::future<Result<ConnectionDetails, TokenSourceError>>(const TokenRequestOptions&)> provider);

  std::future<Result<ConnectionDetails, TokenSourceError>> fetch(const TokenRequestOptions& options,
                                                                 bool force_refresh = false) override;

private:
  explicit CustomTokenSource(
      std::function<std::future<Result<ConnectionDetails, TokenSourceError>>(const TokenRequestOptions&)> provider);

  std::function<std::future<Result<ConnectionDetails, TokenSourceError>>(const TokenRequestOptions&)> provider_;
};

/// @brief Configurable token source that POSTs to a token-server endpoint.
///
/// @see https://docs.livekit.io/frontends/build/authentication/endpoint/
class LIVEKIT_API EndpointTokenSource final : public TokenSourceConfigurable {
public:
  /// @brief Create a token source that fetches credentials from @p endpoint_url.
  static std::unique_ptr<EndpointTokenSource> fromUrl(std::string endpoint_url, TokenEndpointOptions options = {});

  std::future<Result<ConnectionDetails, TokenSourceError>> fetch(const TokenRequestOptions& options,
                                                                 bool force_refresh = false) override;

private:
  EndpointTokenSource(std::string endpoint_url, TokenEndpointOptions options);

  Result<ConnectionDetails, TokenSourceError> fetchSync(const TokenRequestOptions& options) const;

  std::string endpoint_url_;
  TokenEndpointOptions options_;
};

/// @brief Configurable token source for LiveKit Cloud sandbox (dev only).
///
/// @see https://docs.livekit.io/frontends/build/authentication/sandbox-token-server/
class LIVEKIT_API SandboxTokenSource final : public TokenSourceConfigurable {
public:
  /// @brief Create a token source backed by the LiveKit Cloud sandbox token server.
  static std::unique_ptr<SandboxTokenSource> fromSandboxId(const std::string& sandbox_id,
                                                           TokenEndpointOptions options = {});

  std::future<Result<ConnectionDetails, TokenSourceError>> fetch(const TokenRequestOptions& options,
                                                                 bool force_refresh = false) override;

private:
  SandboxTokenSource(const std::string& sandbox_id, TokenEndpointOptions options);

  std::unique_ptr<TokenSourceConfigurable> endpoint_;
};

/// @brief Configurable token source that caches JWT-aware credentials from an inner source.
class LIVEKIT_API CachingTokenSource final : public TokenSourceConfigurable {
public:
  /// @brief Wrap @p inner with JWT-aware caching.
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
