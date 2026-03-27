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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LIVEKIT_RESULT_H
#define LIVEKIT_RESULT_H

#include <cassert>
#include <optional>
#include <utility>
#include <variant>

namespace livekit {

/**
 * Lightweight success-or-error return type for non-exceptional API failures.
 *
 * This is intended for SDK operations where callers are expected to branch on
 * success vs. failure, such as back-pressure or an unpublished track.
 *
 * `Result<T, E>` stores either:
 * - a success value of type `T`, or
 * - an error value of type `E`
 *
 * Accessors are intentionally non-throwing. Calling `value()` on an error
 * result, or `error()` on a success result, is a programmer error and will
 * trip the debug assertion.
 */
template <typename T, typename E> class Result {
public:
  /// Construct a successful result containing a value.
  static Result success(T value) {
    return Result(std::variant<T, E>(std::in_place_index<0>,
                                     std::move(value)));
  }

  /// Construct a failed result containing an error.
  static Result failure(E error) {
    return Result(
        std::variant<T, E>(std::in_place_index<1>, std::move(error)));
  }

  /// True when the result contains a success value.
  bool ok() const noexcept { return storage_.index() == 0; }
  /// True when the result contains an error.
  bool has_error() const noexcept { return !ok(); }
  /// Allows `if (result)` style success checks.
  explicit operator bool() const noexcept { return ok(); }

  /// Access the success value. Requires `ok() == true`.
  T &value() noexcept {
    assert(ok());
    return std::get<0>(storage_);
  }

  /// Access the success value. Requires `ok() == true`.
  const T &value() const noexcept {
    assert(ok());
    return std::get<0>(storage_);
  }

  /// Access the error value. Requires `has_error() == true`.
  E &error() noexcept {
    assert(has_error());
    return std::get<1>(storage_);
  }

  /// Access the error value. Requires `has_error() == true`.
  const E &error() const noexcept {
    assert(has_error());
    return std::get<1>(storage_);
  }

private:
  explicit Result(std::variant<T, E> storage) : storage_(std::move(storage)) {}

  std::variant<T, E> storage_;
};

/**
 * `void` specialization for operations that only report success or failure.
 *
 * This keeps the same calling style as `Result<T, E>` without forcing callers
 * to invent a dummy success payload.
 */
template <typename E> class Result<void, E> {
public:
  /// Construct a successful result with no payload.
  static Result success() { return Result(std::nullopt); }

  /// Construct a failed result containing an error.
  static Result failure(E error) {
    return Result(std::optional<E>(std::move(error)));
  }

  /// True when the operation succeeded.
  bool ok() const noexcept { return !error_.has_value(); }
  /// True when the operation failed.
  bool has_error() const noexcept { return error_.has_value(); }
  /// Allows `if (result)` style success checks.
  explicit operator bool() const noexcept { return ok(); }

  /// Validates success in debug builds. Mirrors the `value()` API shape.
  void value() const noexcept { assert(ok()); }

  /// Access the error value. Requires `has_error() == true`.
  E &error() noexcept {
    assert(has_error());
    return *error_;
  }

  /// Access the error value. Requires `has_error() == true`.
  const E &error() const noexcept {
    assert(has_error());
    return *error_;
  }

private:
  explicit Result(std::optional<E> error) : error_(std::move(error)) {}

  std::optional<E> error_;
};

} // namespace livekit

#endif // LIVEKIT_RESULT_H
