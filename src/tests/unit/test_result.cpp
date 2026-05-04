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
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/// @file test_result.cpp
/// @brief Unit tests for the Result<T, E> and Result<void, E> types.
///
/// Covers the invariants documented in result.h:
///   - ok() / has_error() / bool conversion correctness
///   - value() and error() accessor semantics for lvalue, rvalue, and const
///     overloads
///   - Move construction and forwarding behaviour
///   - void specialization

#include <gtest/gtest.h>
#include <livekit/result.h>

#include <memory>
#include <string>
#include <utility>

namespace livekit {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct SimpleError {
  int code{0};
  std::string message;
};

// ---------------------------------------------------------------------------
// Result<T, E> — success path
// ---------------------------------------------------------------------------

TEST(ResultTest, SuccessOkIsTrue) {
  auto r = Result<int, SimpleError>::success(42);
  EXPECT_TRUE(r.ok());
}

TEST(ResultTest, SuccessHasErrorIsFalse) {
  auto r = Result<int, SimpleError>::success(42);
  EXPECT_FALSE(r.has_error());
}

TEST(ResultTest, SuccessBoolConversionIsTrue) {
  auto r = Result<int, SimpleError>::success(42);
  EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ResultTest, SuccessValueMatchesInput) {
  auto r = Result<int, SimpleError>::success(99);
  EXPECT_EQ(r.value(), 99);
}

TEST(ResultTest, SuccessConstValueMatchesInput) {
  const auto r = Result<int, SimpleError>::success(7);
  EXPECT_EQ(r.value(), 7);
}

TEST(ResultTest, SuccessValueCanBeMutated) {
  auto r = Result<int, SimpleError>::success(1);
  r.value() = 100;
  EXPECT_EQ(r.value(), 100);
}

TEST(ResultTest, SuccessStringValue) {
  auto r = Result<std::string, SimpleError>::success("hello");
  EXPECT_EQ(r.value(), "hello");
}

TEST(ResultTest, SuccessMoveValueTransfersOwnership) {
  auto r = Result<std::unique_ptr<int>, SimpleError>::success(
      std::make_unique<int>(55));
  auto ptr = std::move(r).value();
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 55);
}

// ---------------------------------------------------------------------------
// Result<T, E> — failure path
// ---------------------------------------------------------------------------

TEST(ResultTest, FailureOkIsFalse) {
  auto r = Result<int, SimpleError>::failure(SimpleError{1, "oops"});
  EXPECT_FALSE(r.ok());
}

TEST(ResultTest, FailureHasErrorIsTrue) {
  auto r = Result<int, SimpleError>::failure(SimpleError{1, "oops"});
  EXPECT_TRUE(r.has_error());
}

TEST(ResultTest, FailureBoolConversionIsFalse) {
  auto r = Result<int, SimpleError>::failure(SimpleError{1, "oops"});
  EXPECT_FALSE(static_cast<bool>(r));
}

TEST(ResultTest, FailureErrorCodeMatchesInput) {
  auto r = Result<int, SimpleError>::failure(SimpleError{42, "bad"});
  EXPECT_EQ(r.error().code, 42);
  EXPECT_EQ(r.error().message, "bad");
}

TEST(ResultTest, FailureConstErrorMatchesInput) {
  const auto r = Result<int, SimpleError>::failure(SimpleError{3, "err"});
  EXPECT_EQ(r.error().code, 3);
}

TEST(ResultTest, FailureMoveErrorTransfersOwnership) {
  auto r = Result<int, std::unique_ptr<SimpleError>>::failure(
      std::make_unique<SimpleError>(SimpleError{9, "moved"}));
  auto err = std::move(r).error();
  ASSERT_NE(err, nullptr);
  EXPECT_EQ(err->code, 9);
}

TEST(ResultTest, FailureStringError) {
  auto r = Result<int, std::string>::failure("something went wrong");
  EXPECT_EQ(r.error(), "something went wrong");
}

// ---------------------------------------------------------------------------
// Result<void, E> — success path
// ---------------------------------------------------------------------------

TEST(ResultVoidTest, SuccessOkIsTrue) {
  auto r = Result<void, SimpleError>::success();
  EXPECT_TRUE(r.ok());
}

TEST(ResultVoidTest, SuccessHasErrorIsFalse) {
  auto r = Result<void, SimpleError>::success();
  EXPECT_FALSE(r.has_error());
}

TEST(ResultVoidTest, SuccessBoolConversionIsTrue) {
  auto r = Result<void, SimpleError>::success();
  EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ResultVoidTest, SuccessValueIsCallable) {
  auto r = Result<void, SimpleError>::success();
  EXPECT_NO_THROW(r.value());
}

// ---------------------------------------------------------------------------
// Result<void, E> — failure path
// ---------------------------------------------------------------------------

TEST(ResultVoidTest, FailureOkIsFalse) {
  auto r = Result<void, SimpleError>::failure(SimpleError{5, "void fail"});
  EXPECT_FALSE(r.ok());
}

TEST(ResultVoidTest, FailureHasErrorIsTrue) {
  auto r = Result<void, SimpleError>::failure(SimpleError{5, "void fail"});
  EXPECT_TRUE(r.has_error());
}

TEST(ResultVoidTest, FailureBoolConversionIsFalse) {
  auto r = Result<void, SimpleError>::failure(SimpleError{5, "void fail"});
  EXPECT_FALSE(static_cast<bool>(r));
}

TEST(ResultVoidTest, FailureErrorMatchesInput) {
  auto r = Result<void, SimpleError>::failure(SimpleError{7, "nope"});
  EXPECT_EQ(r.error().code, 7);
  EXPECT_EQ(r.error().message, "nope");
}

TEST(ResultVoidTest, FailureMoveError) {
  auto r = Result<void, std::string>::failure("void error");
  auto msg = std::move(r).error();
  EXPECT_EQ(msg, "void error");
}

// ---------------------------------------------------------------------------
// if-result idiom
// ---------------------------------------------------------------------------

TEST(ResultTest, IfResultIdiomSuccessEntersBranch) {
  auto r = Result<int, SimpleError>::success(1);
  bool entered = false;
  if (r) {
    entered = true;
  }
  EXPECT_TRUE(entered);
}

TEST(ResultTest, IfResultIdiomFailureSkipsBranch) {
  auto r = Result<int, SimpleError>::failure(SimpleError{});
  bool entered = false;
  if (r) {
    entered = true;
  }
  EXPECT_FALSE(entered);
}

} // namespace livekit
