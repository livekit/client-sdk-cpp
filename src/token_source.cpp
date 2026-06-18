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

#include "livekit/token_source.h"

#include <exception>
#include <mutex>
#include <utility>

#include "token_source_internal.h"

namespace livekit {
namespace {

bool tokenRequestOptionsEqual(const TokenRequestOptions& a, const TokenRequestOptions& b) {
  return a.room_name == b.room_name && a.participant_name == b.participant_name &&
         a.participant_identity == b.participant_identity && a.participant_metadata == b.participant_metadata &&
         a.participant_attributes == b.participant_attributes && a.agent_name == b.agent_name &&
         a.agent_metadata == b.agent_metadata && a.agent_deployment == b.agent_deployment;
}

} // namespace

TokenSourceFixed::~TokenSourceFixed() = default;

TokenSourceConfigurable::~TokenSourceConfigurable() = default;

std::unique_ptr<LiteralTokenSource> LiteralTokenSource::fromDetails(ConnectionDetails details) {
  return std::unique_ptr<LiteralTokenSource>(new LiteralTokenSource(std::move(details)));
}

std::unique_ptr<LiteralTokenSource> LiteralTokenSource::fromProvider(
    std::function<std::future<Result<ConnectionDetails, TokenSourceError>>()> provider) {
  return std::unique_ptr<LiteralTokenSource>(new LiteralTokenSource(std::move(provider)));
}

LiteralTokenSource::LiteralTokenSource(ConnectionDetails details) : details_(std::move(details)) {}

LiteralTokenSource::LiteralTokenSource(
    std::function<std::future<Result<ConnectionDetails, TokenSourceError>>()> provider)
    : provider_(std::move(provider)) {}

std::future<Result<ConnectionDetails, TokenSourceError>> LiteralTokenSource::fetch() {
  if (provider_) {
    return provider_();
  }

  return std::async(std::launch::deferred, [details = details_]() {
    if (details.server_url.empty() || details.participant_token.empty()) {
      return Result<ConnectionDetails, TokenSourceError>::failure(
          TokenSourceError{"literal token source returned empty server_url or participant_token"});
    }
    return Result<ConnectionDetails, TokenSourceError>::success(details);
  });
}

std::unique_ptr<CustomTokenSource> CustomTokenSource::fromCallback(
    std::function<std::future<Result<ConnectionDetails, TokenSourceError>>(const TokenRequestOptions&)> provider) {
  return std::unique_ptr<CustomTokenSource>(new CustomTokenSource(std::move(provider)));
}

CustomTokenSource::CustomTokenSource(
    std::function<std::future<Result<ConnectionDetails, TokenSourceError>>(const TokenRequestOptions&)> provider)
    : provider_(std::move(provider)) {}

std::future<Result<ConnectionDetails, TokenSourceError>> CustomTokenSource::fetch(const TokenRequestOptions& options,
                                                                                  bool /*force_refresh*/) {
  return provider_(options);
}

std::unique_ptr<EndpointTokenSource> EndpointTokenSource::fromUrl(std::string endpoint_url,
                                                                  TokenEndpointOptions options) {
  return std::unique_ptr<EndpointTokenSource>(new EndpointTokenSource(std::move(endpoint_url), std::move(options)));
}

EndpointTokenSource::EndpointTokenSource(std::string endpoint_url, TokenEndpointOptions options)
    : endpoint_url_(std::move(endpoint_url)), options_(std::move(options)) {}

std::future<Result<ConnectionDetails, TokenSourceError>> EndpointTokenSource::fetch(const TokenRequestOptions& options,
                                                                                    bool /*force_refresh*/) {
  // NOLINTNEXTLINE(bugprone-exception-escape): std::async may propagate allocation failures from captures.
  return std::async(std::launch::async, [this, options]() {
    try {
      return fetchSync(options);
    } catch (const std::exception& e) {
      return Result<ConnectionDetails, TokenSourceError>::failure(
          TokenSourceError{"token source endpoint fetch failed: " + std::string(e.what())});
    } catch (...) {
      return Result<ConnectionDetails, TokenSourceError>::failure(
          TokenSourceError{"token source endpoint fetch failed: unknown exception"});
    }
  });
}

Result<ConnectionDetails, TokenSourceError> EndpointTokenSource::fetchSync(const TokenRequestOptions& options) const {
  const std::string request_json = buildTokenSourceRequestJson(options);
  auto headers = options_.headers;
  auto http_result = tokenSourceHttpPost(endpoint_url_, headers, request_json, options_.timeout);
  if (!http_result) {
    return Result<ConnectionDetails, TokenSourceError>::failure(
        TokenSourceError{"token server request failed: " + http_result.error()});
  }
  return parseTokenSourceResponseJson(http_result.value());
}

std::unique_ptr<SandboxTokenSource> SandboxTokenSource::fromSandboxId(const std::string& sandbox_id,
                                                                      TokenEndpointOptions options) {
  return std::unique_ptr<SandboxTokenSource>(new SandboxTokenSource(sandbox_id, std::move(options)));
}

SandboxTokenSource::SandboxTokenSource(const std::string& sandbox_id, TokenEndpointOptions options) {
  options.headers["X-Sandbox-ID"] = sandbox_id;
  endpoint_ = EndpointTokenSource::fromUrl("https://cloud-api.livekit.io/api/v2/sandbox/connection-details",
                                           std::move(options));
}

std::future<Result<ConnectionDetails, TokenSourceError>> SandboxTokenSource::fetch(const TokenRequestOptions& options,
                                                                                   bool force_refresh) {
  return endpoint_->fetch(options, force_refresh);
}

std::unique_ptr<CachingTokenSource> CachingTokenSource::wrap(std::unique_ptr<TokenSourceConfigurable> inner) {
  return std::unique_ptr<CachingTokenSource>(new CachingTokenSource(std::move(inner)));
}

CachingTokenSource::CachingTokenSource(std::unique_ptr<TokenSourceConfigurable> inner) : inner_(std::move(inner)) {}

std::future<Result<ConnectionDetails, TokenSourceError>> CachingTokenSource::fetch(const TokenRequestOptions& options,
                                                                                   bool force_refresh) {
  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (!force_refresh && cached_details_.has_value() && cached_options_.has_value() &&
        tokenRequestOptionsEqual(*cached_options_, options) &&
        isParticipantTokenValid(cached_details_->participant_token)) {
      return std::async(std::launch::deferred, [details = *cached_details_]() {
        return Result<ConnectionDetails, TokenSourceError>::success(details);
      });
    }
  }

  auto future = inner_->fetch(options, force_refresh);
  // NOLINTNEXTLINE(bugprone-exception-escape): std::async may propagate allocation failures from captures.
  return std::async(std::launch::async, [this, future = std::move(future), options]() mutable {
    try {
      auto result = future.get();
      if (result) {
        const std::scoped_lock<std::mutex> lock(mutex_);
        cached_options_ = options;
        cached_details_ = result.value();
      }
      return result;
    } catch (const std::exception& e) {
      return Result<ConnectionDetails, TokenSourceError>::failure(
          TokenSourceError{"token source cache refresh failed: " + std::string(e.what())});
    } catch (...) {
      return Result<ConnectionDetails, TokenSourceError>::failure(
          TokenSourceError{"token source cache refresh failed: unknown exception"});
    }
  });
}

} // namespace livekit
