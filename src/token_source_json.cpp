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

#include <cctype>
#include <sstream>
#include <string>

#include "token_source_internal.h"

namespace livekit {
namespace {

std::string jsonEscape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

void appendOptionalStringField(std::ostringstream& out, const char* key, const std::optional<std::string>& value) {
  if (!value.has_value() || value->empty()) {
    return;
  }
  if (out.tellp() > 1) {
    out << ',';
  }
  out << '"' << key << "\":\"" << jsonEscape(*value) << '"';
}

std::optional<std::string> extractJsonStringField(const std::string& json, const char* key) {
  const std::string snake_key = std::string("\"") + key + "\":\"";
  const std::string camel_key = std::string("\"") + key + "\":\"";

  std::size_t pos = json.find(snake_key);
  std::size_t key_len = snake_key.size();
  if (pos == std::string::npos) {
    pos = json.find(camel_key);
    key_len = camel_key.size();
  }
  if (pos == std::string::npos) {
    return std::nullopt;
  }

  pos += key_len;
  std::string value;
  for (; pos < json.size(); ++pos) {
    const char ch = json[pos];
    if (ch == '"') {
      break;
    }
    if (ch == '\\' && pos + 1 < json.size()) {
      ++pos;
      value += json[pos];
      continue;
    }
    value += ch;
  }
  return value;
}

} // namespace

std::string buildTokenSourceRequestJson(const TokenRequestOptions& options) {
  std::ostringstream out;
  out << '{';

  appendOptionalStringField(out, "room_name", options.room_name);
  appendOptionalStringField(out, "participant_name", options.participant_name);
  appendOptionalStringField(out, "participant_identity", options.participant_identity);
  appendOptionalStringField(out, "participant_metadata", options.participant_metadata);

  if (!options.participant_attributes.empty()) {
    if (out.tellp() > 1) {
      out << ',';
    }
    out << "\"participant_attributes\":{";
    bool first = true;
    for (const auto& [key, value] : options.participant_attributes) {
      if (key.empty()) {
        continue;
      }
      if (!first) {
        out << ',';
      }
      first = false;
      out << '"' << jsonEscape(key) << "\":\"" << jsonEscape(value) << '"';
    }
    out << '}';
  }

  if (options.agent_name.has_value() || options.agent_metadata.has_value() || options.agent_deployment.has_value()) {
    if (out.tellp() > 1) {
      out << ',';
    }
    out << R"("room_config":{"agents":[{)";
    bool wrote_agent_field = false;
    if (options.agent_name.has_value() && !options.agent_name->empty()) {
      out << R"("agent_name":")" << jsonEscape(*options.agent_name) << '"';
      wrote_agent_field = true;
    }
    if (options.agent_metadata.has_value() && !options.agent_metadata->empty()) {
      if (wrote_agent_field) {
        out << ',';
      }
      out << R"("metadata":")" << jsonEscape(*options.agent_metadata) << '"';
      wrote_agent_field = true;
    }
    if (options.agent_deployment.has_value() && !options.agent_deployment->empty()) {
      if (wrote_agent_field) {
        out << ',';
      }
      out << R"("deployment":")" << jsonEscape(*options.agent_deployment) << '"';
    }
    out << R"(}]})";
  }

  out << '}';
  return out.str();
}

Result<TokenSourceResponse, TokenSourceError> parseTokenSourceResponseJson(const std::string& json) {
  TokenSourceResponse details;

  const auto server_url = extractJsonStringField(json, "server_url");
  if (!server_url.has_value()) {
    const auto camel_url = extractJsonStringField(json, "serverUrl");
    if (!camel_url.has_value() || camel_url->empty()) {
      return Result<TokenSourceResponse, TokenSourceError>::failure(
          TokenSourceError{"token server response missing server_url"});
    }
    details.server_url = *camel_url;
  } else {
    details.server_url = *server_url;
  }

  const auto participant_token = extractJsonStringField(json, "participant_token");
  if (!participant_token.has_value()) {
    const auto camel_token = extractJsonStringField(json, "participantToken");
    if (!camel_token.has_value() || camel_token->empty()) {
      return Result<TokenSourceResponse, TokenSourceError>::failure(
          TokenSourceError{"token server response missing participant_token"});
    }
    details.participant_token = *camel_token;
  } else {
    details.participant_token = *participant_token;
  }

  if (details.server_url.empty() || details.participant_token.empty()) {
    return Result<TokenSourceResponse, TokenSourceError>::failure(
        TokenSourceError{"token server response contained empty server_url or participant_token"});
  }

  return Result<TokenSourceResponse, TokenSourceError>::success(std::move(details));
}

} // namespace livekit
