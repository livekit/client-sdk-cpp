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

#include "livekit/ffi_handle.h"
#include "livekit_ffi.h"

namespace livekit {

FfiHandle::FfiHandle(uintptr_t h) noexcept : handle_(h) {}

FfiHandle::~FfiHandle() { reset(); }

FfiHandle::FfiHandle(FfiHandle &&other) noexcept : handle_(other.release()) {}

FfiHandle &FfiHandle::operator=(FfiHandle &&other) noexcept {
  if (this != &other) {
    reset(other.release());
  }
  return *this;
}

void FfiHandle::reset(uintptr_t new_handle) noexcept {
  if (handle_) {
    livekit_ffi_drop_handle(handle_);
  }
  handle_ = new_handle;
}

uintptr_t FfiHandle::release() noexcept {
  uintptr_t old = handle_;
  handle_ = 0;
  return old;
}

bool FfiHandle::valid() const noexcept { return handle_ != 0; }

uintptr_t FfiHandle::get() const noexcept { return handle_; }

} // namespace livekit
