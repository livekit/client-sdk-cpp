/*
 * Copyright 2023 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the “License”);
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

#include <cassert>

#include "livekit/ffi_client.h"
#include "ffi.pb.h"
#include "livekit_ffi.h"

namespace livekit
{

FfiClient::FfiClient() {
    livekit_ffi_initialize(&LivekitFfiCallback,
                           true,
                           "cpp",
                           "0.0.0-dev");
}

FfiClient::ListenerId FfiClient::AddListener(const FfiClient::Listener& listener) {
    std::lock_guard<std::mutex> guard(lock_);
    FfiClient::ListenerId id = nextListenerId++;
    listeners_[id] = listener;
    return id;
}

void FfiClient::RemoveListener(ListenerId id) {
    std::lock_guard<std::mutex> guard(lock_);
    listeners_.erase(id);
}

proto::FfiResponse FfiClient::SendRequest(const proto::FfiRequest &request) const {
    std::string bytes;
    if (!request.SerializeToString(&bytes) || bytes.empty()) {
      throw std::runtime_error("failed to serialize FfiRequest");
    }
    const uint8_t* resp_ptr = nullptr;
    size_t resp_len = 0;
    FfiHandleId handle = livekit_ffi_request(
        reinterpret_cast<const uint8_t*>(bytes.data()),
        bytes.size(), &resp_ptr, &resp_len);
    std::cout << "receive a handle " <<  handle << std::endl;

   if (handle == INVALID_HANDLE) {
        throw std::runtime_error("failed to send request, received an invalid handle");
    }

    // Ensure we drop the handle exactly once on all paths
    FfiHandle handle_guard(static_cast<uintptr_t>(handle));
    if (!resp_ptr || resp_len == 0) {
        throw std::runtime_error("FFI returned empty response bytes");
    }

    proto::FfiResponse response;
    if (!response.ParseFromArray(resp_ptr, static_cast<int>(resp_len))) {
        throw std::runtime_error("failed to parse FfiResponse");
    }
    return response;
}

void FfiClient::PushEvent(const proto::FfiEvent &event) const {
    // Dispatch the events to the internal listeners
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& [_, listener] : listeners_) {
        listener(event);
    }
}

void LivekitFfiCallback(const uint8_t *buf, size_t len) {
    proto::FfiEvent event;
    event.ParseFromArray(buf, len);

    FfiClient::getInstance().PushEvent(event);
}

// FfiHandle

FfiHandle::FfiHandle(uintptr_t id) : handle(id) {}

FfiHandle::~FfiHandle() {
    if (handle != INVALID_HANDLE) {
        livekit_ffi_drop_handle(handle);
    }
}

}
