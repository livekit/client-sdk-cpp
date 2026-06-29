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

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "token_source_internal.h"

namespace livekit {
namespace {

using nlohmann::json;

// Read a string field, accepting either the snake_case or camelCase spelling.
// Returns the value only when it is a non-empty JSON string.
std::optional<std::string> readStringField(const json& obj, const char* snake_key, const char* camel_key) {
  for (const char* key : {snake_key, camel_key}) {
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_string()) {
      std::string value = it->get<std::string>();
      if (!value.empty()) {
        return value;
      }
    }
  }
  return std::nullopt;
}

} // namespace

std::string buildTokenSourceRequestJson(const TokenRequestOptions& options) {
  json body = json::object();

  const auto set_optional = [&body](const char* key, const std::optional<std::string>& value) {
    if (value.has_value() && !value->empty()) {
      body[key] = *value;
    }
  };

  set_optional("room_name", options.room_name);
  set_optional("participant_name", options.participant_name);
  set_optional("participant_identity", options.participant_identity);
  set_optional("participant_metadata", options.participant_metadata);

  if (!options.participant_attributes.empty()) {
    json attributes = json::object();
    for (const auto& [key, value] : options.participant_attributes) {
      if (!key.empty()) {
        attributes[key] = value;
      }
    }
    body["participant_attributes"] = std::move(attributes);
  }

  if (options.agent_name.has_value() || options.agent_metadata.has_value() || options.agent_deployment.has_value()) {
    json agent = json::object();
    if (options.agent_name.has_value() && !options.agent_name->empty()) {
      agent["agent_name"] = *options.agent_name;
    }
    if (options.agent_metadata.has_value() && !options.agent_metadata->empty()) {
      agent["metadata"] = *options.agent_metadata;
    }
    if (options.agent_deployment.has_value() && !options.agent_deployment->empty()) {
      agent["deployment"] = *options.agent_deployment;
    }
    body["room_config"] = json{{"agents", json::array({std::move(agent)})}};
  }

  return body.dump();
}

Result<TokenSourceResponse, TokenSourceError> parseTokenSourceResponseJson(const std::string& json_text) {
  // Parse without exceptions: malformed input yields a discarded value, which we
  // treat the same as a response missing the required fields.
  const json parsed = json::parse(json_text, nullptr, /*allow_exceptions=*/false);

  TokenSourceResponse details;

  if (parsed.is_object()) {
    if (const auto server_url = readStringField(parsed, "server_url", "serverUrl")) {
      details.server_url = *server_url;
    }
    if (const auto participant_token = readStringField(parsed, "participant_token", "participantToken")) {
      details.participant_token = *participant_token;
    }
    details.participant_name = readStringField(parsed, "participant_name", "participantName");
    details.room_name = readStringField(parsed, "room_name", "roomName");
  }

  if (details.server_url.empty()) {
    return Result<TokenSourceResponse, TokenSourceError>::failure(
        TokenSourceError{"token server response missing server_url"});
  }
  if (details.participant_token.empty()) {
    return Result<TokenSourceResponse, TokenSourceError>::failure(
        TokenSourceError{"token server response missing participant_token"});
  }

  return Result<TokenSourceResponse, TokenSourceError>::success(std::move(details));
}

} // namespace livekit
