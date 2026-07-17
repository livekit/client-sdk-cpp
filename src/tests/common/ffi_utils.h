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

#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "ffi.pb.h"
#include "ffi_client.h"

namespace livekit::test {

/// Serializes and dispatches a synthetic FFI event through the real callback entry point.
/// Defined in this header for use across different tests.
inline void emitFfiEvent(const proto::FfiEvent& event) {
  std::string bytes;
  ASSERT_TRUE(event.SerializeToString(&bytes));
  ffiEventCallback(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

} // namespace livekit::test
