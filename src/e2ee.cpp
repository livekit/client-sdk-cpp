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

#include "livekit/e2ee.h"

#include <stdexcept>
#include <utility>

#include "e2ee.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"

namespace livekit {

namespace {

std::string bytesToProtoBytes(const std::vector<std::uint8_t> &b) {
  return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

static std::vector<std::uint8_t> protoBytesToBytes(const std::string &s) {
  return std::vector<std::uint8_t>(s.begin(), s.end());
}

} // namespace

struct E2EEManager::Impl {
  std::uint64_t room_handle = 0;
  E2EEOptions options;
  bool enabled() const { return options.enabled; }
  void managerSetEnabled(bool enabled) {
    options.enabled = enabled;
    proto::FfiRequest req;
    auto *e2 = req.mutable_e2ee();
    e2->set_room_handle(room_handle);
    e2->mutable_manager_set_enabled()->set_enabled(enabled);
    FfiClient::instance().sendRequest(req);
  }

  void setSharedKey(const std::vector<std::uint8_t> &key, int key_index) {
    options.shared_key = key;
    proto::FfiRequest req;
    auto *e2 = req.mutable_e2ee();
    e2->set_room_handle(room_handle);
    auto *set = e2->mutable_set_shared_key();
    set->set_key_index(key_index);
    set->set_shared_key(bytesToProtoBytes(key));
    FfiClient::instance().sendRequest(req);
  }

  std::vector<std::uint8_t> getSharedKey(int key_index) const {
    proto::FfiRequest req;
    auto *e2 = req.mutable_e2ee();
    e2->set_room_handle(room_handle);
    e2->mutable_get_shared_key()->set_key_index(key_index);
    auto resp = FfiClient::instance().sendRequest(req);
    const auto &r = resp.e2ee().get_shared_key();
    if (!r.has_key()) {
      return {};
    }
    return protoBytesToBytes(r.key());
  }

  std::vector<std::uint8_t> ratchetSharedKey(int key_index) {
    proto::FfiRequest req;
    auto *e2 = req.mutable_e2ee();
    e2->set_room_handle(room_handle);
    e2->mutable_ratchet_shared_key()->set_key_index(key_index);
    auto resp = FfiClient::instance().sendRequest(req);
    const auto &r = resp.e2ee().ratchet_shared_key();
    if (!r.has_new_key()) {
      return {};
    }
    return protoBytesToBytes(r.new_key());
  }

  void applyOptionsOnceAfterConnect() {
    if (!options.enabled)
      return;
    managerSetEnabled(true);
    // If user provided a shared key, install it at key index 0.
    if (!options.shared_key.empty()) {
      setSharedKey(options.shared_key, /*key_index=*/0);
    }
    // Note, ratchet_window_size / ratchet_salt / failure_tolerance) must be
    // sent as part of connect options (RoomOptions -> E2eeOptions) room.cpp /
    // connect request, not here.
  }
};

E2EEManager::E2EEManager(std::uint64_t room_handle, E2EEOptions options)
    : impl_(std::make_unique<Impl>()) {
  impl_->room_handle = room_handle;
  impl_->options = std::move(options);
  impl_->applyOptionsOnceAfterConnect();
}

E2EEManager::~E2EEManager() = default;
E2EEManager::E2EEManager(E2EEManager &&) noexcept = default;
E2EEManager &E2EEManager::operator=(E2EEManager &&) noexcept = default;

bool E2EEManager::enabled() const { return impl_->enabled(); }

void E2EEManager::setEnabled(bool enabled) {
  impl_->managerSetEnabled(enabled);
}

void E2EEManager::setSharedKey(const std::vector<std::uint8_t> &key,
                               int key_index) {
  impl_->setSharedKey(key, key_index);
}

std::vector<std::uint8_t> E2EEManager::exportSharedKey(int key_index) const {
  return impl_->getSharedKey(key_index);
}

std::vector<std::uint8_t> E2EEManager::ratchetSharedKey(int key_index) {
  return impl_->ratchetSharedKey(key_index);
}

} // namespace livekit
