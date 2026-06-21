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
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "token_source_internal.h"

namespace livekit {
namespace {

std::optional<std::vector<std::uint8_t>> base64UrlDecode(const std::string& input) {
  std::string normalized;
  normalized.reserve(input.size());
  for (const char ch : input) {
    if (ch == '-') {
      normalized += '+';
    } else if (ch == '_') {
      normalized += '/';
    } else {
      normalized += ch;
    }
  }

  while (normalized.size() % 4 != 0) {
    normalized += '=';
  }

  static const int kDecodeTable[256] = {
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57,
      58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
      16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
      37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

  std::vector<std::uint8_t> output;
  output.reserve(normalized.size() * 3 / 4);

  std::uint32_t buffer = 0;
  int bits = 0;
  for (const unsigned char ch : normalized) {
    if (ch == '=') {
      break;
    }
    const int value = kDecodeTable[ch];
    if (value < 0) {
      return std::nullopt;
    }
    buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      output.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
    }
  }

  return output;
}

std::optional<std::int64_t> extractJsonInt64Field(const std::string& json, const char* key) {
  const std::string needle = std::string("\"") + key + "\":";
  const std::size_t pos = json.find(needle);
  if (pos == std::string::npos) {
    return std::nullopt;
  }

  std::size_t index = pos + needle.size();
  while (index < json.size() && std::isspace(static_cast<unsigned char>(json[index])) != 0) {
    ++index;
  }

  bool negative = false;
  if (index < json.size() && json[index] == '-') {
    negative = true;
    ++index;
  }

  std::int64_t value = 0;
  bool found_digit = false;
  for (; index < json.size(); ++index) {
    const char ch = json[index];
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      break;
    }
    found_digit = true;
    value = value * 10 + (ch - '0');
  }

  if (!found_digit) {
    return std::nullopt;
  }
  return negative ? -value : value;
}

std::optional<std::string> extractJwtPayloadJson(const std::string& token) {
  const std::size_t first_dot = token.find('.');
  if (first_dot == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t second_dot = token.find('.', first_dot + 1);
  if (second_dot == std::string::npos) {
    return std::nullopt;
  }

  const std::string payload_segment = token.substr(first_dot + 1, second_dot - first_dot - 1);
  const auto decoded = base64UrlDecode(payload_segment);
  if (!decoded.has_value() || decoded->empty()) {
    return std::nullopt;
  }

  return std::string(decoded->begin(), decoded->end());
}

} // namespace

bool isParticipantTokenValid(const std::string& participant_token) {
  const auto payload_json = extractJwtPayloadJson(participant_token);
  if (!payload_json.has_value()) {
    return false;
  }

  const auto now_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

  const auto nbf = extractJsonInt64Field(*payload_json, "nbf");
  if (nbf.has_value() && *nbf > now_seconds) {
    return false;
  }

  const auto exp = extractJsonInt64Field(*payload_json, "exp");
  if (exp.has_value()) {
    constexpr std::int64_t kExpiryBufferSeconds = 60;
    if (*exp <= now_seconds + kExpiryBufferSeconds) {
      return false;
    }
  }

  return true;
}

} // namespace livekit
