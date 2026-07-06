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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../common/audio_utils.h"
#include "../common/test_common.h"
#include "../common/video_utils.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr auto kEventWaitTimeout = 20s;
constexpr auto kDuplicateGracePeriod = 500ms;

struct RoomEventCounts {
  std::mutex mutex;
  std::condition_variable cv;
  std::map<std::string, int> participant_connected;
  std::map<std::string, int> participant_disconnected;
  std::map<std::string, int> track_published;
  std::map<std::string, int> track_subscribed;
  std::map<std::string, int> track_unsubscribed;
  std::map<std::string, int> track_unpublished;
  int disconnected = 0;
};

struct RoomEventCountsSnapshot {
  std::map<std::string, int> participant_connected;
  std::map<std::string, int> participant_disconnected;
  std::map<std::string, int> track_published;
  std::map<std::string, int> track_subscribed;
  std::map<std::string, int> track_unsubscribed;
  std::map<std::string, int> track_unpublished;
  int disconnected = 0;
};

RoomEventCountsSnapshot snapshotCounts(RoomEventCounts& counts) {
  const std::scoped_lock<std::mutex> lock(counts.mutex);
  RoomEventCountsSnapshot snapshot;
  snapshot.participant_connected = counts.participant_connected;
  snapshot.participant_disconnected = counts.participant_disconnected;
  snapshot.track_published = counts.track_published;
  snapshot.track_subscribed = counts.track_subscribed;
  snapshot.track_unsubscribed = counts.track_unsubscribed;
  snapshot.track_unpublished = counts.track_unpublished;
  snapshot.disconnected = counts.disconnected;
  return snapshot;
}

void incrementMap(std::map<std::string, int>& counts, const std::string& key) { ++counts[key]; }

class RoomEventCounterDelegate : public RoomDelegate {
public:
  explicit RoomEventCounterDelegate(RoomEventCounts& counts) : counts_(counts) {}

  void onParticipantConnected(Room&, const ParticipantConnectedEvent& event) override {
    if (event.participant == nullptr) {
      return;
    }
    notify([&]() { incrementMap(counts_.participant_connected, event.participant->identity()); });
  }

  void onParticipantDisconnected(Room&, const ParticipantDisconnectedEvent& event) override {
    if (event.participant == nullptr) {
      return;
    }
    notify([&]() { incrementMap(counts_.participant_disconnected, event.participant->identity()); });
  }

  void onTrackPublished(Room&, const TrackPublishedEvent& event) override {
    if (event.publication == nullptr) {
      return;
    }
    notify([&]() { incrementMap(counts_.track_published, event.publication->name()); });
  }

  void onTrackSubscribed(Room&, const TrackSubscribedEvent& event) override {
    if (event.publication == nullptr) {
      return;
    }
    notify([&]() { incrementMap(counts_.track_subscribed, event.publication->name()); });
  }

  void onTrackUnsubscribed(Room&, const TrackUnsubscribedEvent& event) override {
    if (event.publication == nullptr) {
      return;
    }
    notify([&]() { incrementMap(counts_.track_unsubscribed, event.publication->name()); });
  }

  void onTrackUnpublished(Room&, const TrackUnpublishedEvent& event) override {
    if (event.publication == nullptr) {
      return;
    }
    notify([&]() { incrementMap(counts_.track_unpublished, event.publication->name()); });
  }

  void onDisconnected(Room&, const DisconnectedEvent&) override {
    notify([&]() { ++counts_.disconnected; });
  }

private:
  template <typename Fn>
  void notify(Fn&& update) {
    {
      const std::scoped_lock<std::mutex> lock(counts_.mutex);
      update();
    }
    counts_.cv.notify_all();
  }

  RoomEventCounts& counts_;
};

bool waitForMapCountAtLeast(RoomEventCounts& counts, const std::map<std::string, int>& keys, int minimum_count,
                            std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(counts.mutex);
  return counts.cv.wait_for(lock, timeout, [&]() {
    for (const auto& [key, _] : keys) {
      (void)_;
      const auto it = counts.participant_connected.find(key);
      if (it == counts.participant_connected.end() || it->second < minimum_count) {
        return false;
      }
    }
    return true;
  });
}

bool waitForMapCountAtLeastTrack(RoomEventCounts& counts, const std::map<std::string, int>& expected,
                                 std::map<std::string, int> RoomEventCounts::* member,
                                 std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(counts.mutex);
  return counts.cv.wait_for(lock, timeout, [&]() {
    const auto& actual = counts.*member;
    for (const auto& [key, minimum_count] : expected) {
      const auto it = actual.find(key);
      if (it == actual.end() || it->second < minimum_count) {
        return false;
      }
    }
    return true;
  });
}

void expectMapCountsExact(const std::map<std::string, int>& actual, const std::map<std::string, int>& expected,
                          const char* label) {
  for (const auto& [key, expected_count] : expected) {
    const auto it = actual.find(key);
    const int actual_count = it == actual.end() ? 0 : it->second;
    EXPECT_EQ(actual_count, expected_count) << label << " count mismatch for key: " << key;
  }
}

void expectCountsUnchangedAfterGrace(RoomEventCounts& counts, const RoomEventCountsSnapshot& before,
                                     const char* phase) {
  std::this_thread::sleep_for(kDuplicateGracePeriod);
  const RoomEventCountsSnapshot after = snapshotCounts(counts);

  expectMapCountsExact(after.participant_connected, before.participant_connected,
                       (std::string(phase) + " participant_connected duplicate").c_str());
  expectMapCountsExact(after.participant_disconnected, before.participant_disconnected,
                       (std::string(phase) + " participant_disconnected duplicate").c_str());
  expectMapCountsExact(after.track_published, before.track_published,
                       (std::string(phase) + " track_published duplicate").c_str());
  expectMapCountsExact(after.track_subscribed, before.track_subscribed,
                       (std::string(phase) + " track_subscribed duplicate").c_str());
  expectMapCountsExact(after.track_unsubscribed, before.track_unsubscribed,
                       (std::string(phase) + " track_unsubscribed duplicate").c_str());
  expectMapCountsExact(after.track_unpublished, before.track_unpublished,
                       (std::string(phase) + " track_unpublished duplicate").c_str());
  EXPECT_EQ(after.disconnected, before.disconnected) << phase << " onDisconnected duplicate";
}

std::string makeUniqueTrackName(const std::string& prefix) { return prefix + "-" + std::to_string(getTimestampUs()); }

std::string describeCounts(const std::map<std::string, int>& counts) {
  std::ostringstream out;
  bool first = true;
  out << "{";
  for (const auto& [key, count] : counts) {
    if (!first) {
      out << ", ";
    }
    first = false;
    out << key << ": " << count;
  }
  out << "}";
  return out.str();
}

class MediaLoopGuard {
public:
  MediaLoopGuard() = default;
  MediaLoopGuard(const MediaLoopGuard&) = delete;
  MediaLoopGuard& operator=(const MediaLoopGuard&) = delete;

  ~MediaLoopGuard() { stop(); }

  void addAudioSource(const std::shared_ptr<AudioSource>& source) {
    threads_.emplace_back([this, source]() {
      runToneLoop(source, running_, 440.0, false, kDefaultAudioSampleRate, kDefaultAudioChannels);
    });
  }

  void addVideoSource(const std::shared_ptr<VideoSource>& source) {
    threads_.emplace_back([this, source]() { runVideoLoop(source, running_, fillWebcamWrapper); });
  }

  void stop() {
    running_.store(false, std::memory_order_relaxed);
    for (auto& thread : threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

private:
  std::atomic<bool> running_{true};
  std::vector<std::thread> threads_;
};

class PublishedTrackGuard {
public:
  explicit PublishedTrackGuard(LocalParticipant* participant) : participant_(participant) {}
  PublishedTrackGuard(const PublishedTrackGuard&) = delete;
  PublishedTrackGuard& operator=(const PublishedTrackGuard&) = delete;

  ~PublishedTrackGuard() noexcept {
    try {
      unpublishAll();
    } catch (...) {
    }
  }

  void addTrackSid(const std::string& sid) {
    if (!sid.empty()) {
      track_sids_.push_back(sid);
    }
  }

  void unpublishAll() {
    if (participant_ != nullptr) {
      for (const auto& sid : track_sids_) {
        if (!sid.empty()) {
          participant_->unpublishTrack(sid);
        }
      }
    }
    track_sids_.clear();
  }

private:
  LocalParticipant* participant_ = nullptr;
  std::vector<std::string> track_sids_;
};

} // namespace

class RoomEventDeduplicationIntegrationTest : public LiveKitTestBase, public ::testing::WithParamInterface<bool> {
protected:
  void SetUp() override {
    LiveKitTestBase::SetUp();
    if (!config_.available) {
      GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B not set";
    }
  }
};

TEST_P(RoomEventDeduplicationIntegrationTest, RoomLifecycleDelegateCallbacksFireExactlyOnce) {
  const bool single_peer_connection = GetParam();

  RoomOptions options;
  options.auto_subscribe = true;
  options.single_peer_connection = single_peer_connection;

  RoomEventCounts observer_counts;
  RoomEventCounterDelegate observer_delegate(observer_counts);

  Room observer_room;
  observer_room.setDelegate(&observer_delegate);
  ASSERT_TRUE(observer_room.connect(config_.url, config_.token_b, options)) << "Observer failed to connect";
  ASSERT_FALSE(observer_room.localParticipant().expired());

  Room peer_room;
  ASSERT_TRUE(peer_room.connect(config_.url, config_.token_a, options)) << "Peer failed to connect";
  ASSERT_FALSE(peer_room.localParticipant().expired());

  const std::string peer_identity = lockLocalParticipant(peer_room)->identity();
  ASSERT_FALSE(peer_identity.empty());

  const std::map<std::string, int> peer_identity_expected{{peer_identity, 1}};
  ASSERT_TRUE(waitForMapCountAtLeast(observer_counts, peer_identity_expected, 1, kEventWaitTimeout))
      << "Timed out waiting for onParticipantConnected";
  ASSERT_TRUE(waitForParticipant(&observer_room, peer_identity, 10s)) << "Peer not visible to observer room";
  {
    const RoomEventCountsSnapshot snapshot = snapshotCounts(observer_counts);
    expectMapCountsExact(snapshot.participant_connected, peer_identity_expected, "onParticipantConnected");
    expectCountsUnchangedAfterGrace(observer_counts, snapshot, "after participant connected");
  }

  const std::string audio_track_name = makeUniqueTrackName("dedupe-audio");
  const std::string video_track_name = makeUniqueTrackName("dedupe-video");

  auto audio_source = std::make_shared<AudioSource>(kDefaultAudioSampleRate, kDefaultAudioChannels, 0);
  auto video_source = std::make_shared<VideoSource>(kDefaultVideoWidth, kDefaultVideoHeight);
  auto audio_track = LocalAudioTrack::createLocalAudioTrack(audio_track_name, audio_source);
  auto video_track = LocalVideoTrack::createLocalVideoTrack(video_track_name, video_source);

  TrackPublishOptions audio_opts;
  audio_opts.source = TrackSource::SOURCE_MICROPHONE;
  TrackPublishOptions video_opts;
  video_opts.source = TrackSource::SOURCE_CAMERA;

  auto peer_participant = lockLocalParticipant(peer_room);
  PublishedTrackGuard published_tracks(peer_participant.get());
  MediaLoopGuard media_loops;

  ASSERT_NO_THROW(peer_participant->publishTrack(audio_track, audio_opts));
  ASSERT_NE(audio_track->publication(), nullptr);
  published_tracks.addTrackSid(audio_track->publication()->sid());
  media_loops.addAudioSource(audio_source);

  ASSERT_NO_THROW(peer_participant->publishTrack(video_track, video_opts));
  ASSERT_NE(video_track->publication(), nullptr);
  published_tracks.addTrackSid(video_track->publication()->sid());
  media_loops.addVideoSource(video_source);

  const std::map<std::string, int> expected_subscribed_counts{{audio_track_name, 1}, {video_track_name, 1}};

  ASSERT_TRUE(waitForMapCountAtLeastTrack(observer_counts, expected_subscribed_counts,
                                          &RoomEventCounts::track_subscribed, kEventWaitTimeout))
      << "Timed out waiting for onTrackSubscribed; observed track_subscribed="
      << describeCounts(snapshotCounts(observer_counts).track_subscribed);

  {
    const RoomEventCountsSnapshot snapshot = snapshotCounts(observer_counts);
    expectMapCountsExact(snapshot.track_subscribed, expected_subscribed_counts, "onTrackSubscribed");
    expectCountsUnchangedAfterGrace(observer_counts, snapshot, "after track subscribe");
  }

  media_loops.stop();
  published_tracks.unpublishAll();

  const std::map<std::string, int> expected_unsubscribed_counts{{audio_track_name, 1}, {video_track_name, 1}};
  ASSERT_TRUE(waitForMapCountAtLeastTrack(observer_counts, expected_unsubscribed_counts,
                                          &RoomEventCounts::track_unsubscribed, kEventWaitTimeout))
      << "Timed out waiting for onTrackUnsubscribed; observed track_unsubscribed="
      << describeCounts(snapshotCounts(observer_counts).track_unsubscribed);

  {
    const RoomEventCountsSnapshot snapshot = snapshotCounts(observer_counts);
    expectMapCountsExact(snapshot.track_unsubscribed, expected_unsubscribed_counts, "onTrackUnsubscribed");
    expectCountsUnchangedAfterGrace(observer_counts, snapshot, "after track unsubscribed");
  }

  peer_room.disconnect();

  ASSERT_TRUE(waitForMapCountAtLeastTrack(observer_counts, peer_identity_expected,
                                          &RoomEventCounts::participant_disconnected, kEventWaitTimeout))
      << "Timed out waiting for onParticipantDisconnected";

  {
    const RoomEventCountsSnapshot snapshot = snapshotCounts(observer_counts);
    expectMapCountsExact(snapshot.participant_disconnected, peer_identity_expected, "onParticipantDisconnected");
    expectCountsUnchangedAfterGrace(observer_counts, snapshot, "after participant disconnected");
  }

  ASSERT_TRUE(observer_room.disconnect()) << "Observer disconnect failed";
  EXPECT_EQ(snapshotCounts(observer_counts).disconnected, 1) << "onDisconnected should fire exactly once";

  const RoomEventCountsSnapshot after_disconnect = snapshotCounts(observer_counts);
  expectCountsUnchangedAfterGrace(observer_counts, after_disconnect, "after observer disconnect");

  EXPECT_FALSE(observer_room.disconnect()) << "Second disconnect should be a no-op";
  EXPECT_EQ(snapshotCounts(observer_counts).disconnected, 1) << "onDisconnected must not double-fire";
}

INSTANTIATE_TEST_SUITE_P(SingleAndDualPeerConnection, RoomEventDeduplicationIntegrationTest, ::testing::Bool());

} // namespace livekit::test
