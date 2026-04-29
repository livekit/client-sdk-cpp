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

#include <memory>

#include <gtest/gtest.h>

#include "livekit/local_track_publication.h"
#include "livekit/remote_track_publication.h"
#include "livekit/track.h"
#include "livekit/track_publication.h"
#include "track.pb.h"

namespace livekit::test {
namespace {

// Concrete `Track` for testing. The base ctor is `protected`, so a thin
// subclass is needed to construct one. The handle is intentionally zero so
// no FFI drop is invoked on destruction.
class FakeTrack : public Track {
public:
  FakeTrack()
      : Track(FfiHandle(), "fake-sid", "fake-name", TrackKind::KIND_AUDIO,
              StreamState::STATE_ACTIVE,
              /*muted=*/false, /*remote=*/false) {}
};

// Default-construct the proto: the publication ctor only reads via getters,
// which return well-defined defaults for unset fields. Using a zero handle
// keeps the `FfiHandle` a no-op on destruction.
proto::OwnedTrackPublication makeOwnedPub() {
  return proto::OwnedTrackPublication{};
}

template <typename Pub>
class TrackPublicationTest : public ::testing::Test {};

using PublicationTypes =
    ::testing::Types<LocalTrackPublication, RemoteTrackPublication>;
TYPED_TEST_SUITE(TrackPublicationTest, PublicationTypes);

} // namespace

TYPED_TEST(TrackPublicationTest, TrackIsNullByDefault) {
  TypeParam pub(makeOwnedPub());
  EXPECT_EQ(pub.track(), nullptr);
}

TYPED_TEST(TrackPublicationTest, SetTrackRoundTrips) {
  TypeParam pub(makeOwnedPub());

  auto track = std::make_shared<FakeTrack>();
  pub.setTrack(track);

  EXPECT_EQ(pub.track().get(), track.get());
  // Publication holds a strong reference alongside the local one.
  EXPECT_GE(track.use_count(), 2);
}

TYPED_TEST(TrackPublicationTest, SetTrackNullClears) {
  TypeParam pub(makeOwnedPub());

  pub.setTrack(std::make_shared<FakeTrack>());
  ASSERT_NE(pub.track(), nullptr);

  pub.setTrack(nullptr);
  EXPECT_EQ(pub.track(), nullptr);
}

TYPED_TEST(TrackPublicationTest, ReplacingTrackReleasesPrevious) {
  TypeParam pub(makeOwnedPub());

  auto first = std::make_shared<FakeTrack>();
  pub.setTrack(first);
  ASSERT_EQ(pub.track().get(), first.get());

  auto second = std::make_shared<FakeTrack>();
  pub.setTrack(second);

  EXPECT_EQ(pub.track().get(), second.get());
  // The publication no longer references `first`; only the local handle does.
  EXPECT_EQ(first.use_count(), 1);
}

} // namespace livekit::test
