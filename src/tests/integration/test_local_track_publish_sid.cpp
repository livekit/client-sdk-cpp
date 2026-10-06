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

#include <memory>
#include <string>

#include "../common/audio_utils.h"
#include "../common/test_common.h"

namespace livekit::test {
namespace {

void expectTrackSidAssigned(const Track& track, const LocalTrackPublication& publication) {
  const std::string& track_sid = track.sid();
  const std::string& publication_sid = publication.sid();
  EXPECT_NE(track_sid, "TR_unknown");
  EXPECT_FALSE(track_sid.empty());
  EXPECT_EQ(track_sid, publication_sid);
}

} // namespace

class LocalTrackPublishSidTest : public LiveKitTestBase {};

TEST_F(LocalTrackPublishSidTest, PublishVideoTrackAssignsSid) {
  failIfNotConfigured();

  Room room;
  const RoomOptions room_options;
  ASSERT_TRUE(room.connect(config_.url, config_.token_a, room_options));

  auto source = std::make_shared<EncodedVideoSource>(VideoCodec::H264, 16, 16);
  std::shared_ptr<LocalVideoTrack> track;
  ASSERT_NO_THROW(
      track = lockLocalParticipant(room)->publishVideoTrack("video-sid-check", source, TrackSource::SOURCE_CAMERA));
  ASSERT_NE(track, nullptr);
  ASSERT_NE(track->publication(), nullptr);

  expectTrackSidAssigned(*track, *track->publication());
  lockLocalParticipant(room)->unpublishTrack(track->publication()->sid());
}

TEST_F(LocalTrackPublishSidTest, PublishAudioTrackAssignsSid) {
  failIfNotConfigured();

  Room room;
  const RoomOptions room_options;
  ASSERT_TRUE(room.connect(config_.url, config_.token_a, room_options));

  auto source = std::make_shared<AudioSource>(kDefaultAudioSampleRate, kDefaultAudioChannels, 0);
  std::shared_ptr<LocalAudioTrack> track;
  ASSERT_NO_THROW(
      track = lockLocalParticipant(room)->publishAudioTrack("audio-sid-check", source, TrackSource::SOURCE_MICROPHONE));
  ASSERT_NE(track, nullptr);
  ASSERT_NE(track->publication(), nullptr);

  expectTrackSidAssigned(*track, *track->publication());
  lockLocalParticipant(room)->unpublishTrack(track->publication()->sid());
}

} // namespace livekit::test
