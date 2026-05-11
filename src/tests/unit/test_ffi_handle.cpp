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

#include <cstdint>
#include <utility>

#include <livekit/ffi_handle.h>

namespace livekit::test {

TEST(FfiHandleTest, DefaultConstructedIsInvalid) {
  FfiHandle handle;
  EXPECT_EQ(handle.get(), 0u);
  EXPECT_FALSE(handle.valid());
  EXPECT_FALSE(static_cast<bool>(handle));
}

TEST(FfiHandleTest, ExplicitZeroIsInvalid) {
  FfiHandle handle(0);
  EXPECT_FALSE(handle.valid());
}

TEST(FfiHandleTest, ResetToZeroIsSafe) {
  FfiHandle handle;
  handle.reset();
  EXPECT_FALSE(handle.valid());
}

TEST(FfiHandleTest, ReleaseReturnsCurrentHandleAndClears) {
  FfiHandle handle;
  const std::uintptr_t released = handle.release();
  EXPECT_EQ(released, 0u);
  EXPECT_FALSE(handle.valid());
}

TEST(FfiHandleTest, MoveTransfersOwnership) {
  FfiHandle src;
  FfiHandle dst(std::move(src));
  EXPECT_FALSE(dst.valid());
  EXPECT_FALSE(src.valid()); // NOLINT(bugprone-use-after-move)

  FfiHandle assigned;
  assigned = std::move(dst);
  EXPECT_FALSE(assigned.valid());
}

} // namespace livekit::test
