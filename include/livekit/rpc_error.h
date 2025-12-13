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

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace livekit {

namespace proto {
class RpcError;
}

/**
 * Specialized error type for RPC methods.
 *
 * Instances of this type, when thrown in a method handler, will have their
 * `code`, `message`, and optional `data` serialized into a proto::RpcError
 * and sent across the wire. The caller will receive an equivalent error
 * on the other side.
 *
 * Built-in errors are included (codes 1400–1999) but developers may use
 * arbitrary codes as well.
 */
class RpcError : public std::runtime_error {
public:
  /**
   * Built-in error codes
   */
  enum class ErrorCode : std::uint32_t {
    APPLICATION_ERROR = 1500,
    CONNECTION_TIMEOUT = 1501,
    RESPONSE_TIMEOUT = 1502,
    RECIPIENT_DISCONNECTED = 1503,
    RESPONSE_PAYLOAD_TOO_LARGE = 1504,
    SEND_FAILED = 1505,

    UNSUPPORTED_METHOD = 1400,
    RECIPIENT_NOT_FOUND = 1401,
    REQUEST_PAYLOAD_TOO_LARGE = 1402,
    UNSUPPORTED_SERVER = 1403,
    UNSUPPORTED_VERSION = 1404,
  };

  /**
   * Construct an RpcError with an explicit numeric code.
   *
   * @param code     Error code value. Codes 1001–1999 are reserved for
   *                 built-in errors (see ErrorCode).
   * @param message  Human-readable error message.
   * @param data     Optional extra data, e.g. JSON. Empty string means no data.
   */
  RpcError(std::uint32_t code, std::string message, std::string data = {});

  /**
   * Construct an RpcError from a built-in ErrorCode.
   *
   * @param code     Built-in error code.
   * @param message  Human-readable error message.
   * @param data     Optional extra data, e.g. JSON. Empty string means no data.
   */
  RpcError(ErrorCode code, std::string message, std::string data = {});

  /**
   * Numeric error code.
   *
   * Codes 1001–1999 are reserved for built-in errors. For built-ins, this
   * value matches the underlying ErrorCode enum value.
   */
  std::uint32_t code() const noexcept;

  /**
   * Human-readable error message.
   */
  const std::string &message() const noexcept;

  /**
   * Optional extra data associated with the error (JSON recommended).
   * May be an empty string if no data was provided.
   */
  const std::string &data() const noexcept;

  /**
   * Create a built-in RpcError using a predefined ErrorCode and default
   * message text.
   *
   * @param code  Built-in error code.
   * @param data  Optional extra data payload (JSON recommended).
   */
  static RpcError builtIn(ErrorCode code, const std::string &data = {});

protected:
  // ----- Protected: only used by LocalParticipant (internal SDK code) -----
  proto::RpcError toProto() const;
  static RpcError fromProto(const proto::RpcError &err);

  friend class LocalParticipant;
  friend class FfiClient;

private:
  static const char *defaultMessageFor(ErrorCode code);

  std::uint32_t code_;
  std::string message_;
  std::string data_;
};

} // namespace livekit