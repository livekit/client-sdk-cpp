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

#pragma once

#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace livekit {

/// Lightweight success-or-error return type for non-exceptional API failures.
///
/// This is intended for SDK operations where callers are expected to branch on
/// success vs. failure, such as back-pressure or an unpublished track.
///
/// `Result<T, E>` stores either:
/// - a success value of type `T`, or
/// - an error value of type `E`
///
/// Accessors validate their preconditions before returning. Calling `value()`
/// on an error result, or `error()` on a success result, throws `std::logic_error`.
/// Avoid this by checking `ok()` / `hasError()` / if (result) before calling `value()` or `error()`.
template <typename T, typename E>
class [[nodiscard]] Result {
public:
  /// Construct a successful result containing a value.
  template <typename U = T, typename = std::enable_if_t<std::is_constructible<T, U&&>::value>>
  static Result success(U&& value) {
    return Result(std::variant<T, E>(std::in_place_index<0>, std::forward<U>(value)));
  }

  /// Construct a failed result containing an error.
  template <typename F = E, typename = std::enable_if_t<std::is_constructible<E, F&&>::value>>
  static Result failure(F&& error) {
    return Result(std::variant<T, E>(std::in_place_index<1>, std::forward<F>(error)));
  }

  /// True when the result contains a success value.
  bool ok() const noexcept { return storage_.index() == 0; }
  /// True when the result contains an error.
  bool hasError() const noexcept { return !ok(); }
  /// Allows `if (result)` style success checks.
  explicit operator bool() const noexcept { return ok(); }

  /// @deprecated Use hasError() instead.
  // NOLINTBEGIN(readability-identifier-naming)
  [[deprecated("Result::has_error is deprecated; use Result::hasError instead")]]
  bool has_error() const noexcept {
    return hasError();
  }
  // NOLINTEND(readability-identifier-naming)

  /// Access the success value.
  ///
  /// @throws std::logic_error if `ok() == false`.
  T& value() & {
    if (!ok()) {
      throw std::logic_error("Result::value() called on an error result");
    }
    return std::get<0>(storage_);
  }

  /// Access the success value.
  ///
  /// @throws std::logic_error if `ok() == false`.
  const T& value() const& {
    if (!ok()) {
      throw std::logic_error("Result::value() called on an error result");
    }
    return std::get<0>(storage_);
  }

  /// Move the success value out.
  ///
  /// @throws std::logic_error if `ok() == false`.
  T&& value() && {
    if (!ok()) {
      throw std::logic_error("Result::value() called on an error result");
    }
    return std::get<0>(std::move(storage_));
  }

  /// Move the success value out.
  ///
  /// @throws std::logic_error if `ok() == false`.
  const T&& value() const&& {
    if (!ok()) {
      throw std::logic_error("Result::value() called on an error result");
    }
    return std::get<0>(std::move(storage_));
  }

  /// Access the error value.
  ///
  /// @throws std::logic_error if `hasError() == false`.
  E& error() & {
    if (!hasError()) {
      throw std::logic_error("Result::error() called on a success result");
    }
    return std::get<1>(storage_);
  }

  /// Access the error value.
  ///
  /// @throws std::logic_error if `hasError() == false`.
  const E& error() const& {
    if (!hasError()) {
      throw std::logic_error("Result::error() called on a success result");
    }
    return std::get<1>(storage_);
  }

  /// Move the error value out.
  ///
  /// @throws std::logic_error if `hasError() == false`.
  E&& error() && {
    if (!hasError()) {
      throw std::logic_error("Result::error() called on a success result");
    }
    return std::get<1>(std::move(storage_));
  }

  /// Move the error value out.
  ///
  /// @throws std::logic_error if `hasError() == false`.
  const E&& error() const&& {
    if (!hasError()) {
      throw std::logic_error("Result::error() called on a success result");
    }
    return std::get<1>(std::move(storage_));
  }

private:
  explicit Result(std::variant<T, E> storage) : storage_(std::move(storage)) {}

  std::variant<T, E> storage_;
};

/// `void` specialization for operations that only report success or failure.
///
/// This keeps the same calling style as `Result<T, E>` without forcing callers
/// to invent a dummy success payload.
template <typename E>
class [[nodiscard]] Result<void, E> {
public:
  /// Construct a successful result with no payload.
  static Result success() { return Result(std::nullopt); }

  /// Construct a failed result containing an error.
  template <typename F = E, typename = std::enable_if_t<std::is_constructible<E, F&&>::value>>
  static Result failure(F&& error) {
    return Result(std::optional<E>(std::forward<F>(error)));
  }

  /// True when the operation succeeded.
  bool ok() const noexcept { return !error_.has_value(); }
  /// True when the operation failed.
  bool hasError() const noexcept { return error_.has_value(); }
  /// Allows `if (result)` style success checks.
  explicit operator bool() const noexcept { return ok(); }

  /// Validates success. Mirrors the `value()` API shape on the primary
  /// template so generic code can use the same form for both.
  ///
  /// @throws std::logic_error if `ok() == false`.
  void value() const {
    if (!ok()) {
      throw std::logic_error("Result::value() called on an error result");
    }
  }

  /// Access the error value.
  ///
  /// @throws std::logic_error if `hasError() == false`.
  E& error() & {
    if (!error_.has_value()) {
      throw std::logic_error("Result::error() called on a success result");
    }
    return *error_;
  }

  /// Access the error value.
  ///
  /// @throws std::logic_error if `hasError() == false`.
  const E& error() const& {
    if (!error_.has_value()) {
      throw std::logic_error("Result::error() called on a success result");
    }
    return *error_;
  }

  /// Move the error value out.
  ///
  /// @throws std::logic_error if `hasError() == false`.
  E&& error() && {
    if (!error_.has_value()) {
      throw std::logic_error("Result::error() called on a success result");
    }
    return std::move(*error_);
  }

  /// Move the error value out.
  ///
  /// @throws std::logic_error if `hasError() == false`.
  const E&& error() const&& {
    if (!error_.has_value()) {
      throw std::logic_error("Result::error() called on a success result");
    }
    return std::move(*error_);
  }

private:
  explicit Result(std::optional<E> error) : error_(std::move(error)) {}

  std::optional<E> error_;
};

} // namespace livekit
