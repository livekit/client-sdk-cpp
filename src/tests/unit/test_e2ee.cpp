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

#include <vector>

#include <livekit/e2ee.h>

namespace livekit::test {

TEST(E2EETest, KeyProviderOptionsDefaults) {
  KeyProviderOptions options;
  EXPECT_FALSE(options.shared_key.has_value());
  EXPECT_FALSE(options.ratchet_salt.empty());
  EXPECT_EQ(options.ratchet_window_size, kDefaultRatchetWindowSize);
  EXPECT_EQ(options.failure_tolerance, kDefaultFailureTolerance);
}

TEST(E2EETest, E2EEOptionsDefaults) {
  E2EEOptions options;
  EXPECT_EQ(options.encryption_type, EncryptionType::GCM);
}

TEST(E2EETest, FrameCryptorAccessors) {
  E2EEManager::FrameCryptor cryptor(/*room_handle=*/0, "alice", /*key_index=*/3,
                                    /*enabled=*/true);
  EXPECT_EQ(cryptor.participantIdentity(), "alice");
  EXPECT_EQ(cryptor.keyIndex(), 3);
  EXPECT_TRUE(cryptor.enabled());
}

} // namespace livekit::test
