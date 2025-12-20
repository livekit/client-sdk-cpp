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
#include "livekit/ffi_handle.h"

namespace livekit {

namespace {

std::string bytesToString(const std::vector<std::uint8_t> &v) {
  return std::string(reinterpret_cast<const char *>(v.data()), v.size());
}

std::vector<std::uint8_t> stringToBytes(const std::string &s) {
  return std::vector<std::uint8_t>(s.begin(), s.end());
}

} // namespace

// ============================================================================
// KeyProvider
// ============================================================================

E2EEManager::KeyProvider::KeyProvider(std::uint64_t room_handle,
                                      EncryptionKeyProviderOptions options)
    : room_handle_(room_handle), options_(std::move(options)) {}

const EncryptionKeyProviderOptions &E2EEManager::KeyProvider::options() const {
  return options_;
}

void E2EEManager::KeyProvider::setSharedKey(
    const std::vector<std::uint8_t> &key, int key_index) {
  proto::FfiRequest req;
  req.mutable_e2ee()->set_room_handle(room_handle_);
  req.mutable_e2ee()->mutable_set_shared_key()->set_key_index(key_index);
  req.mutable_e2ee()->mutable_set_shared_key()->set_shared_key(
      bytesToString(key));
  FfiClient::instance().sendRequest(req);
}

std::vector<std::uint8_t>
E2EEManager::KeyProvider::exportSharedKey(int key_index) const {
  proto::FfiRequest req;
  req.mutable_e2ee()->set_room_handle(room_handle_);
  req.mutable_e2ee()->mutable_get_shared_key()->set_key_index(key_index);
  auto resp = FfiClient::instance().sendRequest(req);
  return stringToBytes(resp.e2ee().get_shared_key().key());
}

std::vector<std::uint8_t>
E2EEManager::KeyProvider::ratchetSharedKey(int key_index) {
  proto::FfiRequest req;
  req.mutable_e2ee()->set_room_handle(room_handle_);
  req.mutable_e2ee()->mutable_ratchet_shared_key()->set_key_index(key_index);
  auto resp = FfiClient::instance().sendRequest(req);
  return stringToBytes(resp.e2ee().ratchet_shared_key().new_key());
}

void E2EEManager::KeyProvider::setKey(const std::string &participant_identity,
                                      const std::vector<std::uint8_t> &key,
                                      int key_index) {
  proto::FfiRequest req;
  req.mutable_e2ee()->set_room_handle(room_handle_);
  req.mutable_e2ee()->mutable_set_key()->set_participant_identity(
      participant_identity);
  req.mutable_e2ee()->mutable_set_key()->set_key_index(key_index);
  req.mutable_e2ee()->mutable_set_key()->set_key(bytesToString(key));
  FfiClient::instance().sendRequest(req);
}

std::vector<std::uint8_t>
E2EEManager::KeyProvider::exportKey(const std::string &participant_identity,
                                    int key_index) const {
  proto::FfiRequest req;
  req.mutable_e2ee()->set_room_handle(room_handle_);
  req.mutable_e2ee()->mutable_get_key()->set_participant_identity(
      participant_identity);
  req.mutable_e2ee()->mutable_get_key()->set_key_index(key_index);
  auto resp = FfiClient::instance().sendRequest(req);
  return stringToBytes(resp.e2ee().get_key().key());
}

std::vector<std::uint8_t>
E2EEManager::KeyProvider::ratchetKey(const std::string &participant_identity,
                                     int key_index) {
  proto::FfiRequest req;
  req.mutable_e2ee()->set_room_handle(room_handle_);
  req.mutable_e2ee()->mutable_ratchet_key()->set_participant_identity(
      participant_identity);
  req.mutable_e2ee()->mutable_ratchet_key()->set_key_index(key_index);
  auto resp = FfiClient::instance().sendRequest(req);
  return stringToBytes(resp.e2ee().ratchet_key().new_key());
}

// ============================================================================
// FrameCryptor
// ============================================================================

E2EEManager::FrameCryptor::FrameCryptor(std::uint64_t room_handle,
                                        std::string participant_identity,
                                        int key_index, bool enabled)
    : room_handle_(room_handle), enabled_(enabled),
      participant_identity_(std::move(participant_identity)),
      key_index_(key_index) {}

const std::string &E2EEManager::FrameCryptor::participantIdentity() const {
  return participant_identity_;
}
int E2EEManager::FrameCryptor::keyIndex() const { return key_index_; }
bool E2EEManager::FrameCryptor::enabled() const { return enabled_; }

void E2EEManager::FrameCryptor::setEnabled(bool enabled) {
  proto::FfiRequest req;
  req.mutable_e2ee()->set_room_handle(room_handle_);
  req.mutable_e2ee()->mutable_cryptor_set_enabled()->set_participant_identity(
      participant_identity_);
  req.mutable_e2ee()->mutable_cryptor_set_enabled()->set_enabled(enabled);
  FfiClient::instance().sendRequest(req);
}

void E2EEManager::FrameCryptor::setKeyIndex(int key_index) {
  proto::FfiRequest req;
  req.mutable_e2ee()->set_room_handle(room_handle_);
  req.mutable_e2ee()->mutable_cryptor_set_key_index()->set_participant_identity(
      participant_identity_);
  req.mutable_e2ee()->mutable_cryptor_set_key_index()->set_key_index(key_index);
  FfiClient::instance().sendRequest(req);
}

// ============================================================================
// E2EEManager
// ============================================================================

E2EEManager::E2EEManager(std::uint64_t room_handle, const E2EEOptions &options)
    : room_handle_(room_handle),
      enabled_(true), // or false, depending on your desired default behavior
      options_(options),
      key_provider_(room_handle, options.key_provider_options) {}

bool E2EEManager::enabled() const { return enabled_; }

void E2EEManager::setEnabled(bool enabled) {
  proto::FfiRequest req;
  req.mutable_e2ee()->set_room_handle(room_handle_);
  req.mutable_e2ee()->mutable_manager_set_enabled()->set_enabled(enabled);
  FfiClient::instance().sendRequest(req);
}

E2EEManager::KeyProvider *E2EEManager::keyProvider() { return &key_provider_; }
const E2EEManager::KeyProvider *E2EEManager::keyProvider() const {
  return &key_provider_;
}

std::vector<E2EEManager::FrameCryptor> E2EEManager::frameCryptors() const {
  proto::FfiRequest req;
  req.mutable_e2ee()->set_room_handle(room_handle_);
  auto resp = FfiClient::instance().sendRequest(req);
  std::vector<E2EEManager::FrameCryptor> out;
  const auto &list = resp.e2ee().manager_get_frame_cryptors().frame_cryptors();
  out.reserve(static_cast<std::size_t>(list.size()));
  for (const auto &fc : list) {
    out.emplace_back(room_handle_, fc.participant_identity(), fc.key_index(),
                     fc.enabled());
  }
  return out;
}

} // namespace livekit
