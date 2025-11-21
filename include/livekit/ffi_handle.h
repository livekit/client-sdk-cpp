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

namespace livekit {

/**
 * @brief RAII wrapper for an FFI handle (uintptr_t) coming from Rust.
 *
 * Ensures that the handle is automatically released via
 * livekit_ffi_drop_handle() when the object goes out of scope.
 */
class FfiHandle {
public:
  explicit FfiHandle(uintptr_t h = 0) noexcept;
  ~FfiHandle();

  // Non-copyable
  FfiHandle(const FfiHandle &) = delete;
  FfiHandle &operator=(const FfiHandle &) = delete;

  // Movable
  FfiHandle(FfiHandle &&other) noexcept;
  FfiHandle &operator=(FfiHandle &&other) noexcept;

  // Replace the current handle with a new one, dropping the old if needed
  void reset(uintptr_t new_handle = 0) noexcept;

  // Release ownership of the handle without dropping it
  [[nodiscard]] uintptr_t release() noexcept;

  // Whether the handle is valid (non-zero)
  [[nodiscard]] bool valid() const noexcept;

  // Get the raw handle value
  [[nodiscard]] uintptr_t get() const noexcept;

  // Allow `if (handle)` syntax
  explicit operator bool() const noexcept { return valid(); }

private:
  uintptr_t handle_{0};
};

} // namespace livekit