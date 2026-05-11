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

#include <livekit/rpc_error.h>

namespace livekit::test {

TEST(RpcErrorTest, NumericConstructor) {
  RpcError err(1234, "boom", "{\"detail\":\"x\"}");
  EXPECT_EQ(err.code(), 1234u);
  EXPECT_EQ(err.message(), "boom");
  EXPECT_EQ(err.data(), "{\"detail\":\"x\"}");
}

TEST(RpcErrorTest, EnumConstructor) {
  RpcError err(RpcError::ErrorCode::APPLICATION_ERROR, "handler failed");
  EXPECT_EQ(err.code(), static_cast<std::uint32_t>(RpcError::ErrorCode::APPLICATION_ERROR));
  EXPECT_EQ(err.message(), "handler failed");
  EXPECT_TRUE(err.data().empty());
}

TEST(RpcErrorTest, BuiltInFactory) {
  RpcError err = RpcError::builtIn(RpcError::ErrorCode::CONNECTION_TIMEOUT);
  EXPECT_EQ(err.code(), static_cast<std::uint32_t>(RpcError::ErrorCode::CONNECTION_TIMEOUT));
  EXPECT_FALSE(err.message().empty());
  EXPECT_TRUE(err.data().empty());
}

TEST(RpcErrorTest, ThrowableAsRuntimeError) {
  try {
    throw RpcError(RpcError::ErrorCode::SEND_FAILED, "send failed");
  } catch (const std::runtime_error& e) {
    SUCCEED() << "caught as std::runtime_error: " << e.what();
    return;
  }
  FAIL() << "RpcError did not propagate as std::runtime_error";
}

} // namespace livekit::test
