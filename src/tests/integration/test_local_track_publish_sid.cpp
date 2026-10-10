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

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

#include "../common/audio_utils.h"
#include "../common/room_test_access.h"
#include "../common/test_common.h"
#include "room.pb.h"

namespace livekit::test {
namespace {

using namespace std::chrono_literals;

class LocalPublicationDelegate final : public RoomDelegate {
public:
  void onLocalTrackPublished(Room&, const LocalTrackPublishedEvent& event) override {
    if (!event.publication) {
      return;
    }
    {
      const std::scoped_lock<std::mutex> guard(mutex_);
      published_sids_.insert(event.publication->sid());
    }
    cv_.notify_all();
  }

  bool waitForPublished(const std::string& sid, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] { return published_sids_.count(sid) != 0; });
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::unordered_set<std::string> published_sids_;
};

void expectTrackSidAssigned(const Track& track, const LocalTrackPublication& publication) {
  const std::string& track_sid = track.sid();
  const std::string& publication_sid = publication.sid();
  EXPECT_NE(track_sid, "TR_unknown");
  EXPECT_FALSE(track_sid.empty());
  EXPECT_EQ(track_sid, publication_sid);
}

bool waitForSidChange(const Track& track, const std::string& previous_sid, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (track.sid() != previous_sid && track.sid() != "TR_unknown") {
      return true;
    }
    std::this_thread::sleep_for(50ms);
  }
  return track.sid() != previous_sid && track.sid() != "TR_unknown";
}

} // namespace

class LocalTrackPublishSidTest : public LiveKitTestBase {};

TEST_F(LocalTrackPublishSidTest, PublishVideoTrackAssignsSid) {
  failIfNotConfigured();

  Room room;
  LocalPublicationDelegate delegate;
  room.setDelegate(&delegate);
  const RoomOptions room_options;
  ASSERT_TRUE(room.connect(config_.url, config_.token_a, room_options));

  auto source = std::make_shared<EncodedVideoSource>(VideoCodec::H264, 16, 16);
  std::shared_ptr<LocalVideoTrack> track;
  ASSERT_NO_THROW(
      track = lockLocalParticipant(room)->publishVideoTrack("video-sid-check", source, TrackSource::SOURCE_CAMERA));
  ASSERT_NE(track, nullptr);
  ASSERT_NE(track->publication(), nullptr);

  expectTrackSidAssigned(*track, *track->publication());
  EXPECT_TRUE(delegate.waitForPublished(track->sid(), 5s));
  lockLocalParticipant(room)->unpublishTrack(track->publication()->sid());
}

TEST_F(LocalTrackPublishSidTest, PublishAudioTrackAssignsSid) {
  failIfNotConfigured();

  Room room;
  LocalPublicationDelegate delegate;
  room.setDelegate(&delegate);
  const RoomOptions room_options;
  ASSERT_TRUE(room.connect(config_.url, config_.token_a, room_options));

  auto source = std::make_shared<AudioSource>(kDefaultAudioSampleRate, kDefaultAudioChannels, 0);
  std::shared_ptr<LocalAudioTrack> track;
  ASSERT_NO_THROW(
      track = lockLocalParticipant(room)->publishAudioTrack("audio-sid-check", source, TrackSource::SOURCE_MICROPHONE));
  ASSERT_NE(track, nullptr);
  ASSERT_NE(track->publication(), nullptr);

  expectTrackSidAssigned(*track, *track->publication());
  EXPECT_TRUE(delegate.waitForPublished(track->sid(), 5s));
  lockLocalParticipant(room)->unpublishTrack(track->publication()->sid());
}

TEST_F(LocalTrackPublishSidTest, FullReconnectUpdatesPublishedSid) {
  failIfNotConfigured();

  Room room;
  const RoomOptions room_options;
  ASSERT_TRUE(room.connect(config_.url, config_.token_a, room_options));

  auto source = std::make_shared<EncodedVideoSource>(VideoCodec::H264, 16, 16);
  std::shared_ptr<LocalVideoTrack> track;
  ASSERT_NO_THROW(
      track = lockLocalParticipant(room)->publishVideoTrack("republish-sid-check", source, TrackSource::SOURCE_CAMERA));
  ASSERT_NE(track, nullptr);
  ASSERT_NE(track->publication(), nullptr);
  expectTrackSidAssigned(*track, *track->publication());

  const std::string previous_sid = track->sid();
  ASSERT_NO_THROW(RoomTestAccess::simulateScenario(room, proto::SIMULATE_FULL_RECONNECT));
  ASSERT_TRUE(waitForSidChange(*track, previous_sid, 30s)) << "Timed out waiting for republished track SID";

  ASSERT_NE(track->publication(), nullptr);
  expectTrackSidAssigned(*track, *track->publication());
  EXPECT_NE(track->sid(), previous_sid);

  const auto pubs = lockLocalParticipant(room)->trackPublications();
  EXPECT_EQ(pubs.count(previous_sid), 0u);
  EXPECT_EQ(pubs.count(track->sid()), 1u);

  lockLocalParticipant(room)->unpublishTrack(track->sid());
}

} // namespace livekit::test
