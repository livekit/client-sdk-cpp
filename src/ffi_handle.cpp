#include "livekit/ffi_handle.h"
#include "livekit_ffi.h" 

namespace livekit {

FfiHandle::FfiHandle(uintptr_t h) noexcept : handle_(h) {}

FfiHandle::~FfiHandle() {
  reset();
}

FfiHandle::FfiHandle(FfiHandle&& other) noexcept : handle_(other.release()) {}

FfiHandle& FfiHandle::operator=(FfiHandle&& other) noexcept {
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

bool FfiHandle::valid() const noexcept {
  return handle_ != 0;
}

uintptr_t FfiHandle::get() const noexcept {
  return handle_;
}

}  // namespace livekit
