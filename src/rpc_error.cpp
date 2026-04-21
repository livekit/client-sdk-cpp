/*
 * Copyright 2025 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "livekit/rpc_error.h"

#include "rpc.pb.h"

namespace livekit {

RpcError::RpcError(std::uint32_t code, std::string message, std::string data)
    : std::runtime_error(message), code_(code), message_(std::move(message)),
      data_(std::move(data)) {}

RpcError::RpcError(ErrorCode code, std::string message, std::string data)
    : RpcError(static_cast<std::uint32_t>(code), std::move(message),
               std::move(data)) {}

std::uint32_t RpcError::code() const noexcept { return code_; }

const std::string &RpcError::message() const noexcept { return message_; }

const std::string &RpcError::data() const noexcept { return data_; }

proto::RpcError RpcError::toProto() const {
  proto::RpcError err;
  err.set_code(code_);
  err.set_message(message_);

  // Set data only if non-empty; empty string means "no data".
  if (!data_.empty()) {
    err.set_data(data_);
  }

  return err;
}

RpcError RpcError::fromProto(const proto::RpcError &err) {
  // proto::RpcError.data() will return empty string if unset, which is fine.
  return RpcError(err.code(), err.message(), err.data());
}

RpcError RpcError::builtIn(ErrorCode code, const std::string &data) {
  const char *msg = defaultMessageFor(code);
  return RpcError(code, msg ? std::string(msg) : std::string{}, data);
}

const char *RpcError::defaultMessageFor(ErrorCode code) {
  switch (code) {
  case ErrorCode::APPLICATION_ERROR:
    return "Application error in method handler";
  case ErrorCode::CONNECTION_TIMEOUT:
    return "Connection timeout";
  case ErrorCode::RESPONSE_TIMEOUT:
    return "Response timeout";
  case ErrorCode::RECIPIENT_DISCONNECTED:
    return "Recipient disconnected";
  case ErrorCode::RESPONSE_PAYLOAD_TOO_LARGE:
    return "Response payload too large";
  case ErrorCode::SEND_FAILED:
    return "Failed to send";
  case ErrorCode::UNSUPPORTED_METHOD:
    return "Method not supported at destination";
  case ErrorCode::RECIPIENT_NOT_FOUND:
    return "Recipient not found";
  case ErrorCode::REQUEST_PAYLOAD_TOO_LARGE:
    return "Request payload too large";
  case ErrorCode::UNSUPPORTED_SERVER:
    return "RPC not supported by server";
  case ErrorCode::UNSUPPORTED_VERSION:
    return "Unsupported RPC version";
  }

  // Should be unreachable if all enum values are covered.
  return "";
}

} // namespace livekit