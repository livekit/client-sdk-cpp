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

#include <gtest/gtest.h>

#include <string>
#include <utility>

#include <livekit/result.h>

namespace livekit::test {

struct StubError {
  int code = 0;
  std::string message;
};

TEST(ResultTest, ValueResultSuccess) {
  auto r = Result<int, StubError>::success(42);
  ASSERT_TRUE(r.ok());
  EXPECT_FALSE(r.has_error());
  EXPECT_TRUE(static_cast<bool>(r));
  EXPECT_EQ(r.value(), 42);
}

TEST(ResultTest, ValueResultFailure) {
  auto r = Result<int, StubError>::failure(StubError{7, "nope"});
  ASSERT_FALSE(r.ok());
  EXPECT_TRUE(r.has_error());
  EXPECT_EQ(r.error().code, 7);
  EXPECT_EQ(r.error().message, "nope");
}

TEST(ResultTest, VoidResultSuccess) {
  auto r = Result<void, StubError>::success();
  ASSERT_TRUE(r.ok());
  EXPECT_FALSE(r.has_error());
  r.value();
}

TEST(ResultTest, VoidResultFailure) {
  auto r = Result<void, StubError>::failure(StubError{1, "bad"});
  ASSERT_FALSE(r.ok());
  EXPECT_EQ(r.error().code, 1);
  StubError moved = std::move(r).error();
  EXPECT_EQ(moved.message, "bad");
}

} // namespace livekit::test
