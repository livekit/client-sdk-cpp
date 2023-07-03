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
    InitializeRequest *initRequest = new InitializeRequest;
    initRequest->set_event_callback_ptr(reinterpret_cast<uint64_t>(&LivekitFfiCallback));

    FfiRequest request{};
    request.set_allocated_initialize(initRequest);
    SendRequest(request);
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

FfiResponse FfiClient::SendRequest(const FfiRequest &request) const {
    size_t len = request.ByteSizeLong();
    uint8_t *buf = new uint8_t[len];
    assert(request.SerializeToArray(buf, len));

    const uint8_t **res_ptr = new const uint8_t*;
    size_t *res_len = new size_t;
    
    auto handle = livekit_ffi_request(buf, len, res_ptr, res_len);

    delete[] buf;
    if (handle == INVALID_HANDLE) {
        delete res_ptr;
        delete res_len;
        throw std::runtime_error("failed to send request, received an invalid handle");
    }
    FfiHandle _handle(handle);

    FfiResponse response;
    assert(response.ParseFromArray(*res_ptr, *res_len));
    delete res_ptr;
    delete res_len;

    return response;
}

void FfiClient::PushEvent(const FfiEvent &event) const {
    // Dispatch the events to the internal listeners
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& [_, listener] : listeners_) {
        listener(event);
    }
}

void LivekitFfiCallback(const uint8_t *buf, size_t len) {
    FfiEvent event;
    assert(event.ParseFromArray(buf, len));

    FfiClient::getInstance().PushEvent(event);
}

// FfiHandle

FfiHandle::FfiHandle(uintptr_t id) : handle(id) {}

FfiHandle::~FfiHandle() {
    if (handle != INVALID_HANDLE) {
        // assert(livekit_ffi_drop_handle(handle));
    }
}

}
