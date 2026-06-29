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

#include "livekit/token_source.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <mutex>
#include <utility>

#include "token_source_internal.h"

namespace livekit {
namespace {

using TokenSourceResult = Result<TokenSourceResponse, TokenSourceError>;
using TokenSourceFuture = std::future<TokenSourceResult>;
constexpr const char* kDefaultSandboxBaseUrl = "https://cloud-api.livekit.io";

bool tokenRequestOptionsEqual(const TokenRequestOptions& a, const TokenRequestOptions& b) {
  return a.room_name == b.room_name && a.participant_name == b.participant_name &&
         a.participant_identity == b.participant_identity && a.participant_metadata == b.participant_metadata &&
         a.participant_attributes == b.participant_attributes && a.agent_name == b.agent_name &&
         a.agent_metadata == b.agent_metadata && a.agent_deployment == b.agent_deployment;
}

TokenSourceFuture makeFailedFuture(std::string message) {
  std::promise<TokenSourceResult> promise;
  promise.set_value(TokenSourceResult::failure(TokenSourceError{std::move(message)}));
  return promise.get_future();
}

template <typename WorkFn>
TokenSourceFuture runAsyncTokenSource(std::string context, WorkFn&& work_fn) {
  try {
    return std::async(std::launch::async,
                      [context = std::move(context), work_fn = std::forward<WorkFn>(work_fn)]() mutable {
                        try {
                          return work_fn();
                        } catch (const std::exception& e) {
                          return TokenSourceResult::failure(TokenSourceError{context + ": " + std::string(e.what())});
                        } catch (...) {
                          return TokenSourceResult::failure(TokenSourceError{context + ": unknown exception"});
                        }
                      });
  } catch (const std::exception& e) {
    return makeFailedFuture(context + ": failed to start async work: " + std::string(e.what()));
  } catch (...) {
    return makeFailedFuture(context + ": failed to start async work: unknown exception");
  }
}

std::string trimSandboxId(const std::string& sandbox_id) {
  const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  const auto begin = std::find_if_not(sandbox_id.begin(), sandbox_id.end(), is_space);
  const auto end = std::find_if_not(sandbox_id.rbegin(), sandbox_id.rend(), is_space).base();
  if (begin >= end) {
    return {};
  }
  return std::string(begin, end);
}

std::string joinUrlPath(const std::string& base_url, const std::string& path) {
  if (base_url.empty()) {
    return path;
  }
  if (base_url.back() == '/') {
    return base_url + (path.empty() || path.front() == '/' ? path.substr(path.front() == '/' ? 1 : 0) : path);
  }
  if (path.empty()) {
    return base_url;
  }
  if (path.front() == '/') {
    return base_url + path;
  }
  return base_url + "/" + path;
}

struct ResolvedSandboxEndpoint {
  std::string url;
  TokenEndpointOptions options;
};

// Apply the sandbox header and resolve the connection-details URL shared by the
// production and test-only sandbox factories.
ResolvedSandboxEndpoint resolveSandboxEndpoint(const std::string& sandbox_id, TokenEndpointOptions options,
                                               const std::string& base_url) {
  options.headers["X-Sandbox-ID"] = trimSandboxId(sandbox_id);
  const std::string resolved_base_url = base_url.empty() ? kDefaultSandboxBaseUrl : base_url;
  return {joinUrlPath(resolved_base_url, "/api/v2/sandbox/connection-details"), std::move(options)};
}

} // namespace

std::unique_ptr<LiteralTokenSource> LiteralTokenSource::fromLiteral(std::string server_url,
                                                                    std::string participant_token) {
  TokenSourceResponse details;
  details.server_url = std::move(server_url);
  details.participant_token = std::move(participant_token);
  return std::unique_ptr<LiteralTokenSource>(new LiteralTokenSource(std::move(details)));
}

std::unique_ptr<LiteralTokenSource> LiteralTokenSource::fromProvider(
    std::function<std::future<Result<TokenSourceResponse, TokenSourceError>>()> provider) {
  return std::unique_ptr<LiteralTokenSource>(new LiteralTokenSource(std::move(provider)));
}

LiteralTokenSource::LiteralTokenSource(TokenSourceResponse details) : details_(std::move(details)) {}

LiteralTokenSource::LiteralTokenSource(
    std::function<std::future<Result<TokenSourceResponse, TokenSourceError>>()> provider)
    : provider_(std::move(provider)) {}

std::future<Result<TokenSourceResponse, TokenSourceError>> LiteralTokenSource::fetch() {
  if (provider_) {
    return provider_();
  }

  return std::async(std::launch::deferred, [details = details_]() {
    if (details.server_url.empty() || details.participant_token.empty()) {
      return Result<TokenSourceResponse, TokenSourceError>::failure(
          TokenSourceError{"literal token source returned empty server_url or participant_token"});
    }
    return Result<TokenSourceResponse, TokenSourceError>::success(details);
  });
}

std::unique_ptr<CustomTokenSource> CustomTokenSource::fromCustom(
    std::function<std::future<Result<TokenSourceResponse, TokenSourceError>>(const TokenRequestOptions&)> provider) {
  return std::unique_ptr<CustomTokenSource>(new CustomTokenSource(std::move(provider)));
}

CustomTokenSource::CustomTokenSource(
    std::function<std::future<Result<TokenSourceResponse, TokenSourceError>>(const TokenRequestOptions&)> provider)
    : provider_(std::move(provider)) {}

std::future<Result<TokenSourceResponse, TokenSourceError>> CustomTokenSource::fetch(
    const TokenRequestOptions& options) {
  return provider_(options);
}

std::unique_ptr<EndpointTokenSource> EndpointTokenSource::fromEndpoint(std::string endpoint_url,
                                                                       TokenEndpointOptions options) {
  return std::unique_ptr<EndpointTokenSource>(
      new EndpointTokenSource(std::move(endpoint_url), std::move(options), &tokenSourceHttpRequest));
}

EndpointTokenSource::EndpointTokenSource(std::string endpoint_url, TokenEndpointOptions options,
                                         HttpTransport transport)
    : endpoint_url_(std::move(endpoint_url)), options_(std::move(options)), transport_(std::move(transport)) {}

std::unique_ptr<EndpointTokenSource> EndpointTokenSourceTestAccess::create(std::string endpoint_url,
                                                                           TokenEndpointOptions options,
                                                                           TokenSourceHttpTransport transport) {
  return std::unique_ptr<EndpointTokenSource>(
      new EndpointTokenSource(std::move(endpoint_url), std::move(options), std::move(transport)));
}

std::future<Result<TokenSourceResponse, TokenSourceError>> EndpointTokenSource::fetch(
    const TokenRequestOptions& options) {
  std::shared_ptr<TokenRequestOptions> options_snapshot;
  try {
    options_snapshot = std::make_shared<TokenRequestOptions>(options);
  } catch (const std::exception& e) {
    return makeFailedFuture("token source endpoint fetch failed: failed to copy request options: " +
                            std::string(e.what()));
  } catch (...) {
    return makeFailedFuture("token source endpoint fetch failed: failed to copy request options: unknown exception");
  }

  return runAsyncTokenSource("token source endpoint fetch failed",
                             [this, options_snapshot]() { return fetchSync(*options_snapshot); });
}

Result<TokenSourceResponse, TokenSourceError> EndpointTokenSource::fetchSync(const TokenRequestOptions& options) const {
  const std::string request_json = buildTokenSourceRequestJson(options);
  auto headers = options_.headers;
  auto http_result = transport_(options_.method, endpoint_url_, headers, request_json, options_.timeout);
  if (!http_result) {
    return Result<TokenSourceResponse, TokenSourceError>::failure(
        TokenSourceError{"token server request failed: " + http_result.error()});
  }
  return parseTokenSourceResponseJson(http_result.value());
}

std::unique_ptr<SandboxTokenSource> SandboxTokenSource::fromSandboxTokenServer(
    const std::string& sandbox_id, const SandboxTokenServerOptions& options) {
  auto resolved = resolveSandboxEndpoint(sandbox_id, {}, options.base_url);
  auto endpoint = EndpointTokenSource::fromEndpoint(std::move(resolved.url), std::move(resolved.options));
  return std::unique_ptr<SandboxTokenSource>(new SandboxTokenSource(std::move(endpoint)));
}

SandboxTokenSource::SandboxTokenSource(std::unique_ptr<TokenSourceConfigurable> endpoint)
    : endpoint_(std::move(endpoint)) {}

std::unique_ptr<SandboxTokenSource> SandboxTokenSourceTestAccess::create(const std::string& sandbox_id,
                                                                         const SandboxTokenServerOptions& options,
                                                                         TokenSourceHttpTransport transport) {
  auto resolved = resolveSandboxEndpoint(sandbox_id, {}, options.base_url);
  auto endpoint =
      EndpointTokenSourceTestAccess::create(std::move(resolved.url), std::move(resolved.options), std::move(transport));
  return std::unique_ptr<SandboxTokenSource>(new SandboxTokenSource(std::move(endpoint)));
}

std::future<Result<TokenSourceResponse, TokenSourceError>> SandboxTokenSource::fetch(
    const TokenRequestOptions& options) {
  return endpoint_->fetch(options);
}

std::unique_ptr<CachingTokenSource> CachingTokenSource::wrap(std::unique_ptr<TokenSourceConfigurable> inner) {
  return std::unique_ptr<CachingTokenSource>(new CachingTokenSource(std::move(inner)));
}

CachingTokenSource::CachingTokenSource(std::unique_ptr<TokenSourceConfigurable> inner) : inner_(std::move(inner)) {}

std::future<Result<TokenSourceResponse, TokenSourceError>> CachingTokenSource::fetch(
    const TokenRequestOptions& options) {
  std::shared_ptr<TokenRequestOptions> options_snapshot;
  try {
    options_snapshot = std::make_shared<TokenRequestOptions>(options);
  } catch (const std::exception& e) {
    return makeFailedFuture("token source cache fetch failed: failed to copy request options: " +
                            std::string(e.what()));
  } catch (...) {
    return makeFailedFuture("token source cache fetch failed: failed to copy request options: unknown exception");
  }

  return runAsyncTokenSource("token source cache fetch failed", [this, options_snapshot]() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (cached_details_.has_value() && cached_options_.has_value() &&
        tokenRequestOptionsEqual(*cached_options_, *options_snapshot) &&
        isParticipantTokenValid(cached_details_->participant_token)) {
      return TokenSourceResult::success(*cached_details_);
    }

    auto result = inner_->fetch(*options_snapshot).get();
    if (result) {
      cached_options_ = *options_snapshot;
      cached_details_ = result.value();
    }
    return result;
  });
}

void CachingTokenSource::invalidate() {
  const std::scoped_lock<std::mutex> lock(mutex_);
  cached_options_.reset();
  cached_details_.reset();
}

std::optional<TokenSourceResponse> CachingTokenSource::cachedResponse() const {
  const std::scoped_lock<std::mutex> lock(mutex_);
  return cached_details_;
}

} // namespace livekit
