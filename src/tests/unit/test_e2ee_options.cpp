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
#include <livekit/e2ee.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ffi.pb.h"
#include "room_proto_converter.h"

namespace livekit::test {
namespace {

std::string bytesToString(const std::vector<std::uint8_t>& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace

TEST(E2EEOptionsProtoTest, DefaultKeyProviderOptionsPopulateRequiredFields) {
  const KeyProviderOptions options;
  const auto proto_options = toProto(options);

  EXPECT_TRUE(proto_options.IsInitialized());
  EXPECT_FALSE(proto_options.has_shared_key());
  EXPECT_TRUE(proto_options.has_ratchet_window_size());
  EXPECT_TRUE(proto_options.has_ratchet_salt());
  EXPECT_TRUE(proto_options.has_failure_tolerance());
  EXPECT_TRUE(proto_options.has_key_ring_size());
  EXPECT_TRUE(proto_options.has_key_derivation_function());
  EXPECT_EQ(proto_options.ratchet_window_size(), kDefaultRatchetWindowSize);
  EXPECT_EQ(proto_options.ratchet_salt(), kDefaultRatchetSalt);
  EXPECT_EQ(proto_options.failure_tolerance(), kDefaultFailureTolerance);
  EXPECT_EQ(proto_options.key_ring_size(), kDefaultKeyRingSize);
  EXPECT_EQ(proto_options.key_derivation_function(), proto::PBKDF2);
}

TEST(E2EEOptionsProtoTest, CustomKeyProviderOptionsRoundTripToProto) {
  KeyProviderOptions options;
  options.shared_key = std::vector<std::uint8_t>{0x01, 0x02, 0x03};
  options.ratchet_salt = std::vector<std::uint8_t>{0x04, 0x05};
  options.ratchet_window_size = 32;
  options.failure_tolerance = 3;
  options.key_ring_size = 8;
  options.key_derivation_function = KeyDerivationFunction::HKDF;

  const auto proto_options = toProto(options);

  EXPECT_TRUE(proto_options.IsInitialized());
  ASSERT_TRUE(proto_options.has_shared_key());
  EXPECT_EQ(proto_options.shared_key(), bytesToString(*options.shared_key));
  EXPECT_EQ(proto_options.ratchet_salt(), bytesToString(options.ratchet_salt));
  EXPECT_EQ(proto_options.ratchet_window_size(), options.ratchet_window_size);
  EXPECT_EQ(proto_options.failure_tolerance(), options.failure_tolerance);
  EXPECT_EQ(proto_options.key_ring_size(), options.key_ring_size);
  EXPECT_EQ(proto_options.key_derivation_function(), proto::HKDF);
}

TEST(E2EEOptionsProtoTest, ConnectRequestWithEncryptionSerializes) {
  E2EEOptions options;
  options.key_provider_options.shared_key = std::vector<std::uint8_t>{0x0A, 0x0B};

  proto::FfiRequest request;
  auto* connect = request.mutable_connect();
  connect->set_url("ws://localhost:7880");
  connect->set_token("test-token");
  auto* encryption = connect->mutable_options()->mutable_encryption();
  encryption->set_encryption_type(static_cast<proto::EncryptionType>(options.encryption_type));
  encryption->mutable_key_provider_options()->CopyFrom(toProto(options.key_provider_options));

  ASSERT_TRUE(request.IsInitialized()) << request.InitializationErrorString();

  std::string serialized;
  EXPECT_TRUE(request.SerializeToString(&serialized));
  EXPECT_FALSE(serialized.empty());
}

} // namespace livekit::test
