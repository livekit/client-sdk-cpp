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

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "tests/common/test_common.h"

namespace livekit::test {

class RemoteTrackPublicationServerTest : public LiveKitTestBase {};

TEST_F(RemoteTrackPublicationServerTest, PublicationControlsRoundTripAndDeduplicate) {
  failIfNotConfigured();

  Room publisher_room;
  Room subscriber_room;
  const RoomOptions options;

  ASSERT_TRUE(subscriber_room.connect(config_.url, config_.token_b, options));
  ASSERT_TRUE(publisher_room.connect(config_.url, config_.token_a, options));

  const std::string publisher_identity = lockLocalParticipant(publisher_room)->identity();
  ASSERT_TRUE(waitForParticipant(&subscriber_room, publisher_identity, 10s));

  constexpr std::uint32_t kSourceWidth = 640;
  constexpr std::uint32_t kSourceHeight = 360;
  constexpr std::uint32_t kRequestedWidth = 320;
  constexpr std::uint32_t kRequestedHeight = 180;
  const std::string track_name = "remote-dimension-update";

  auto source = std::make_shared<VideoSource>(kSourceWidth, kSourceHeight);
  auto track = LocalVideoTrack::createLocalVideoTrack(track_name, source);
  TrackPublishOptions publish_options;
  publish_options.source = TrackSource::SOURCE_CAMERA;
  publish_options.simulcast = true;
  ASSERT_NO_THROW(lockLocalParticipant(publisher_room)->publishTrack(track, publish_options));

  std::shared_ptr<RemoteTrackPublication> publication;
  const auto subscription_deadline = std::chrono::steady_clock::now() + 10s;
  while (std::chrono::steady_clock::now() < subscription_deadline) {
    auto publisher = subscriber_room.remoteParticipant(publisher_identity).lock();
    if (publisher != nullptr) {
      for (const auto& [sid, candidate] : publisher->trackPublications()) {
        (void)sid;
        if (candidate != nullptr && candidate->name() == track_name && candidate->subscribed() &&
            candidate->track() != nullptr) {
          publication = candidate;
          break;
        }
      }
    }
    if (publication != nullptr) {
      break;
    }
    std::this_thread::sleep_for(10ms);
  }

  ASSERT_NE(publication, nullptr) << "Timed out waiting for remote video subscription";
  ASSERT_EQ(publication->kind(), TrackKind::KIND_VIDEO);
  ASSERT_TRUE(publication->simulcasted());

  EXPECT_TRUE(publication->setVideoDimensions(kRequestedWidth, kRequestedHeight));
  EXPECT_EQ(publication->width(), kRequestedWidth);
  EXPECT_EQ(publication->height(), kRequestedHeight);
  EXPECT_FALSE(publication->setVideoDimensions(kRequestedWidth, kRequestedHeight));

  EXPECT_FALSE(publication->setVideoQuality(static_cast<VideoQuality>(-1)));
  EXPECT_TRUE(publication->setVideoQuality(VideoQuality::LOW));
  EXPECT_EQ(publication->videoQuality(), VideoQuality::LOW);
  EXPECT_FALSE(publication->setVideoQuality(VideoQuality::LOW));
  EXPECT_TRUE(publication->setVideoDimensions(kRequestedWidth, kRequestedHeight));
  EXPECT_EQ(publication->videoQuality(), VideoQuality::HIGH);

  EXPECT_TRUE(publication->setEnabled(false));
  EXPECT_FALSE(publication->enabled());
  EXPECT_FALSE(publication->setEnabled(false));
  EXPECT_TRUE(publication->setEnabled(true));
  EXPECT_TRUE(publication->enabled());

  EXPECT_FALSE(publication->setVideoDimensions(0, kRequestedHeight));

  publication->setSubscribed(false);
  EXPECT_FALSE(publication->setVideoDimensions(kSourceWidth, kSourceHeight));
  EXPECT_FALSE(publication->setVideoQuality(VideoQuality::MEDIUM));
  EXPECT_FALSE(publication->setEnabled(false));

  if (track->publication() != nullptr) {
    lockLocalParticipant(publisher_room)->unpublishTrack(track->publication()->sid());
  }
}

} // namespace livekit::test
