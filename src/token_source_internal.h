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
#include <map>
#include <string>

#include "livekit/result.h"
#include "livekit/token_source.h"
#include "livekit/visibility.h"

namespace livekit {

/// @brief Perform an HTTPS/HTTP POST with a JSON body (internal).
LIVEKIT_INTERNAL_API Result<std::string, std::string> tokenSourceHttpPost(
    const std::string& url, const std::map<std::string, std::string>& headers, const std::string& json_body,
    std::chrono::milliseconds timeout);

/// @brief Build the standard LiveKit token-server JSON request body.
LIVEKIT_INTERNAL_API std::string buildTokenSourceRequestJson(const TokenRequestOptions& options);

/// @brief Parse a token-server JSON response into @ref ConnectionDetails.
LIVEKIT_INTERNAL_API Result<ConnectionDetails, TokenSourceError> parseTokenSourceResponseJson(const std::string& json);

/// @brief Return @c true when the JWT is within its validity window (1-minute skew buffer).
LIVEKIT_INTERNAL_API bool isParticipantTokenValid(const std::string& participant_token);

} // namespace livekit
