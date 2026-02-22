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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <livekit_bridge/livekit_bridge.h>

#include <livekit/track.h>

#include <unordered_map>

namespace livekit_bridge {
namespace test {

class CallbackKeyTest : public ::testing::Test {
protected:
  // Type aliases for convenience -- these are private types in LiveKitBridge,
  // accessible via the friend declaration.
  using CallbackKey = LiveKitBridge::CallbackKey;
  using CallbackKeyHash = LiveKitBridge::CallbackKeyHash;
};

TEST_F(CallbackKeyTest, EqualKeysCompareEqual) {
  CallbackKey a{"alice", livekit::TrackSource::SOURCE_MICROPHONE};
  CallbackKey b{"alice", livekit::TrackSource::SOURCE_MICROPHONE};

  EXPECT_TRUE(a == b) << "Identical keys should compare equal";
}

TEST_F(CallbackKeyTest, DifferentIdentityComparesUnequal) {
  CallbackKey a{"alice", livekit::TrackSource::SOURCE_MICROPHONE};
  CallbackKey b{"bob", livekit::TrackSource::SOURCE_MICROPHONE};

  EXPECT_FALSE(a == b) << "Keys with different identities should not be equal";
}

TEST_F(CallbackKeyTest, DifferentSourceComparesUnequal) {
  CallbackKey a{"alice", livekit::TrackSource::SOURCE_MICROPHONE};
  CallbackKey b{"alice", livekit::TrackSource::SOURCE_CAMERA};

  EXPECT_FALSE(a == b) << "Keys with different sources should not be equal";
}

TEST_F(CallbackKeyTest, EqualKeysProduceSameHash) {
  CallbackKey a{"alice", livekit::TrackSource::SOURCE_MICROPHONE};
  CallbackKey b{"alice", livekit::TrackSource::SOURCE_MICROPHONE};
  CallbackKeyHash hasher;

  EXPECT_EQ(hasher(a), hasher(b))
      << "Equal keys must produce the same hash value";
}

TEST_F(CallbackKeyTest, DifferentKeysProduceDifferentHashes) {
  CallbackKeyHash hasher;

  CallbackKey mic{"alice", livekit::TrackSource::SOURCE_MICROPHONE};
  CallbackKey cam{"alice", livekit::TrackSource::SOURCE_CAMERA};
  CallbackKey bob{"bob", livekit::TrackSource::SOURCE_MICROPHONE};

  // While hash collisions are technically allowed, these simple cases
  // should not collide with a reasonable hash function.
  EXPECT_NE(hasher(mic), hasher(cam))
      << "Different sources should (likely) produce different hashes";
  EXPECT_NE(hasher(mic), hasher(bob))
      << "Different identities should (likely) produce different hashes";
}

TEST_F(CallbackKeyTest, WorksAsUnorderedMapKey) {
  std::unordered_map<CallbackKey, int, CallbackKeyHash> map;

  CallbackKey key1{"alice", livekit::TrackSource::SOURCE_MICROPHONE};
  CallbackKey key2{"bob", livekit::TrackSource::SOURCE_CAMERA};
  CallbackKey key3{"alice", livekit::TrackSource::SOURCE_CAMERA};

  // Insert
  map[key1] = 1;
  map[key2] = 2;
  map[key3] = 3;

  EXPECT_EQ(map.size(), 3u)
      << "Three distinct keys should produce three entries";

  // Find
  EXPECT_EQ(map[key1], 1);
  EXPECT_EQ(map[key2], 2);
  EXPECT_EQ(map[key3], 3);

  // Overwrite
  map[key1] = 42;
  EXPECT_EQ(map[key1], 42) << "Inserting with same key should overwrite";
  EXPECT_EQ(map.size(), 3u) << "Size should not change after overwrite";

  // Erase
  map.erase(key2);
  EXPECT_EQ(map.size(), 2u);
  EXPECT_EQ(map.count(key2), 0u) << "Erased key should not be found";
}

TEST_F(CallbackKeyTest, EmptyIdentityWorks) {
  CallbackKey empty{"", livekit::TrackSource::SOURCE_UNKNOWN};
  CallbackKey also_empty{"", livekit::TrackSource::SOURCE_UNKNOWN};
  CallbackKeyHash hasher;

  EXPECT_TRUE(empty == also_empty);
  EXPECT_EQ(hasher(empty), hasher(also_empty));
}

} // namespace test
} // namespace livekit_bridge
