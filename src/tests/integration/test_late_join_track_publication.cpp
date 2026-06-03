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

#include <livekit/local_data_track.h>
#include <livekit/remote_data_track.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
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

constexpr auto kWaitTimeout = 20s;
constexpr int kAudioTrackCount = 2;
constexpr int kVideoTrackCount = 2;
constexpr int kDataTrackCount = 4;
constexpr int kVideoWidth = 160;
constexpr int kVideoHeight = 90;

struct ExpectedPublication {
  std::string name;
  TrackKind kind = TrackKind::KIND_UNKNOWN;
};

struct LateJoinPublicationState {
  std::mutex mutex;
  std::condition_variable cv;
  std::string expected_publisher_identity;
  std::map<std::string, TrackKind> expected_media_tracks;
  std::set<std::string> expected_data_tracks;
  std::map<std::string, TrackKind> published_media_tracks;
  std::map<std::string, TrackKind> subscribed_media_tracks;
  std::map<std::string, std::string> published_data_tracks;
  std::map<std::string, int> published_media_counts;
  std::map<std::string, int> subscribed_media_counts;
  std::map<std::string, int> published_data_counts;
  std::vector<std::string> invariant_failures;
};

class LateJoinPublicationDelegate : public RoomDelegate {
public:
  explicit LateJoinPublicationDelegate(LateJoinPublicationState& state) : state_(state) {}

  void onTrackPublished(Room& room, const TrackPublishedEvent& event) override {
    if (!event.publication) {
      return;
    }

    validateMediaCallbackState(room, event.publication->name(), event.publication->kind(), "onTrackPublished");

    std::scoped_lock<std::mutex> lock(state_.mutex);
    state_.published_media_tracks[event.publication->name()] = event.publication->kind();
    ++state_.published_media_counts[event.publication->name()];
    state_.cv.notify_all();
  }

  void onTrackSubscribed(Room& room, const TrackSubscribedEvent& event) override {
    if (!event.publication) {
      return;
    }

    validateMediaCallbackState(room, event.publication->name(), event.publication->kind(), "onTrackSubscribed");

    std::scoped_lock<std::mutex> lock(state_.mutex);
    state_.subscribed_media_tracks[event.publication->name()] = event.publication->kind();
    ++state_.subscribed_media_counts[event.publication->name()];
    state_.cv.notify_all();
  }

  void onDataTrackPublished(Room& room, const DataTrackPublishedEvent& event) override {
    if (!event.track) {
      return;
    }

    validateDataCallbackState(room, event.track->info().name, event.track->publisherIdentity(), "onDataTrackPublished");

    std::scoped_lock<std::mutex> lock(state_.mutex);
    state_.published_data_tracks[event.track->info().name] = event.track->publisherIdentity();
    ++state_.published_data_counts[event.track->info().name];
    state_.cv.notify_all();
  }

private:
  static bool hasPublication(RemoteParticipant* participant, const std::string& name, TrackKind kind) {
    if (participant == nullptr) {
      return false;
    }

    for (const auto& [sid, publication] : participant->trackPublications()) {
      (void)sid;
      if (publication && publication->name() == name && publication->kind() == kind) {
        return true;
      }
    }
    return false;
  }

  void recordInvariantFailure(const std::string& message) {
    std::scoped_lock<std::mutex> lock(state_.mutex);
    state_.invariant_failures.push_back(message);
    state_.cv.notify_all();
  }

  void validateCommonCallbackState(Room& room, const std::string& callback_name) {
    if (room.localParticipant().expired()) {
      recordInvariantFailure(callback_name + " fired before room.localParticipant() was initialized");
    }

    std::string expected_publisher_identity;
    {
      std::scoped_lock<std::mutex> lock(state_.mutex);
      expected_publisher_identity = state_.expected_publisher_identity;
    }

    if (!expected_publisher_identity.empty() && room.remoteParticipant(expected_publisher_identity).expired()) {
      recordInvariantFailure(callback_name +
                             " fired before expected remote participant was visible: " + expected_publisher_identity);
    }
  }

  void validateMediaCallbackState(Room& room, const std::string& track_name, TrackKind kind,
                                  const std::string& callback_name) {
    validateCommonCallbackState(room, callback_name);

    std::string expected_publisher_identity;
    bool expected_track = false;
    {
      std::scoped_lock<std::mutex> lock(state_.mutex);
      expected_publisher_identity = state_.expected_publisher_identity;
      const auto it = state_.expected_media_tracks.find(track_name);
      expected_track = it != state_.expected_media_tracks.end() && it->second == kind;
    }

    if (!expected_track) {
      recordInvariantFailure(callback_name + " fired for unexpected media publication: " + track_name);
    }

    auto participant = room.remoteParticipant(expected_publisher_identity).lock();
    if (!hasPublication(participant.get(), track_name, kind)) {
      recordInvariantFailure(callback_name + " fired before expected remote publication was visible: " + track_name);
    }
  }

  void validateDataCallbackState(Room& room, const std::string& track_name, const std::string& publisher_identity,
                                 const std::string& callback_name) {
    validateCommonCallbackState(room, callback_name);

    std::string expected_publisher_identity;
    bool expected_track = false;
    {
      std::scoped_lock<std::mutex> lock(state_.mutex);
      expected_publisher_identity = state_.expected_publisher_identity;
      expected_track = state_.expected_data_tracks.count(track_name) != 0;
    }

    if (!expected_track) {
      recordInvariantFailure(callback_name + " fired for unexpected data publication: " + track_name);
    }
    if (publisher_identity != expected_publisher_identity) {
      recordInvariantFailure(callback_name + " publisher identity mismatch for " + track_name);
    }
  }

  LateJoinPublicationState& state_;
};

class MediaLoopGuard {
public:
  MediaLoopGuard() = default;
  MediaLoopGuard(const MediaLoopGuard&) = delete;
  MediaLoopGuard& operator=(const MediaLoopGuard&) = delete;

  ~MediaLoopGuard() { stop(); }

  void addVideoSource(const std::shared_ptr<VideoSource>& source, bool red_mode) {
    threads_.emplace_back([this, source, red_mode]() {
      runVideoLoop(source, running_, red_mode ? fillRedWrapper : fillWebcamWrapper, kVideoWidth, kVideoHeight);
    });
  }

  void addAudioSource(const std::shared_ptr<AudioSource>& source, double base_freq_hz, bool siren_mode) {
    threads_.emplace_back(
        [this, source, base_freq_hz, siren_mode]() { runToneLoop(source, running_, base_freq_hz, siren_mode); });
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

  ~PublishedTrackGuard() { unpublishAll(); }

  void addMediaTrack(const std::shared_ptr<Track>& track, const std::string& sid) {
    media_tracks_.push_back({track, sid});
  }

  void addDataTrack(const std::shared_ptr<LocalDataTrack>& track) { data_tracks_.push_back(track); }

  void unpublishAll() {
    if (participant_ != nullptr) {
      for (const auto& track : media_tracks_) {
        if (track.track && !track.sid.empty()) {
          participant_->unpublishTrack(track.sid);
        }
      }
    }

    for (const auto& track : data_tracks_) {
      if (track && track->isPublished()) {
        track->unpublishDataTrack();
      }
    }

    media_tracks_.clear();
    data_tracks_.clear();
  }

private:
  struct PublishedMediaTrack {
    std::shared_ptr<Track> track;
    std::string sid;
  };

  LocalParticipant* participant_ = nullptr;
  std::vector<PublishedMediaTrack> media_tracks_;
  std::vector<std::shared_ptr<LocalDataTrack>> data_tracks_;
};

bool hasExpectedMediaSubscriptions(const LateJoinPublicationState& state,
                                   const std::vector<ExpectedPublication>& expected_media) {
  for (const auto& expected : expected_media) {
    const auto subscribed_it = state.subscribed_media_tracks.find(expected.name);
    if (subscribed_it == state.subscribed_media_tracks.end() || subscribed_it->second != expected.kind) {
      return false;
    }
  }
  return true;
}

bool hasExpectedDataPublications(const LateJoinPublicationState& state, const std::set<std::string>& expected_data) {
  for (const auto& name : expected_data) {
    if (state.published_data_tracks.count(name) == 0 || state.published_data_counts.count(name) == 0) {
      return false;
    }
  }
  return true;
}

const char* trackKindName(TrackKind kind) {
  switch (kind) {
    case TrackKind::KIND_AUDIO:
      return "audio";
    case TrackKind::KIND_VIDEO:
      return "video";
    case TrackKind::KIND_UNKNOWN:
      break;
  }
  return "unknown";
}

std::string describeMediaTracks(const std::map<std::string, TrackKind>& tracks) {
  std::ostringstream out;
  bool first = true;
  for (const auto& [name, kind] : tracks) {
    if (!first) {
      out << ", ";
    }
    first = false;
    out << name << "=" << trackKindName(kind);
  }
  return tracks.empty() ? "<none>" : out.str();
}

std::string describeDataTracks(const std::map<std::string, std::string>& tracks) {
  std::ostringstream out;
  bool first = true;
  for (const auto& [name, publisher_identity] : tracks) {
    if (!first) {
      out << ", ";
    }
    first = false;
    out << name << "=" << publisher_identity;
  }
  return tracks.empty() ? "<none>" : out.str();
}

std::string describeInvariantFailures(const std::vector<std::string>& failures) {
  std::ostringstream out;
  for (std::size_t i = 0; i < failures.size(); ++i) {
    if (i != 0) {
      out << "; ";
    }
    out << failures[i];
  }
  return failures.empty() ? "<none>" : out.str();
}

std::string makeTrackName(const std::string& prefix, int index) {
  return prefix + "-" + std::to_string(index) + "-" + std::to_string(getTimestampUs());
}

} // namespace

class LateJoinTrackPublicationIntegrationTest : public LiveKitTestBase, public ::testing::WithParamInterface<bool> {
protected:
  void SetUp() override {
    LiveKitTestBase::SetUp();
    if (!config_.available) {
      throw std::runtime_error("LateJoinTrackPublicationIntegrationTest: test configuration not set up");
    }
  }
};

TEST_P(LateJoinTrackPublicationIntegrationTest, ConsumerReceivesAlreadyPublishedAudioTrackEvents) {
  const bool single_peer_connection = GetParam();
  RoomOptions options;
  options.auto_subscribe = true;
  options.single_peer_connection = single_peer_connection;

  Room publisher_room;
  ASSERT_TRUE(publisher_room.connect(config_.url, config_.token_a, options)) << "Publisher failed to connect";
  ASSERT_FALSE(publisher_room.localParticipant().expired());

  const std::string publisher_identity = lockLocalParticipant(publisher_room)->identity();
  ASSERT_FALSE(publisher_identity.empty());

  PublishedTrackGuard published_tracks(lockLocalParticipant(publisher_room).get());
  MediaLoopGuard media_loops;
  std::vector<ExpectedPublication> expected_media;

  for (int i = 0; i < kAudioTrackCount; ++i) {
    const std::string track_name = makeTrackName("late-join-audio", i);
    auto source = std::make_shared<AudioSource>(kDefaultAudioSampleRate, kDefaultAudioChannels, 0);
    auto track = LocalAudioTrack::createLocalAudioTrack(track_name, source);
    TrackPublishOptions publish_options;
    publish_options.source = TrackSource::SOURCE_MICROPHONE;

    ASSERT_NO_THROW(lockLocalParticipant(publisher_room)->publishTrack(track, publish_options));
    ASSERT_NE(track->publication(), nullptr) << "Audio track was not locally published";

    published_tracks.addMediaTrack(track, track->publication()->sid());
    media_loops.addAudioSource(source, 320.0 + static_cast<double>(i) * 60.0, i % 2 == 1);
    expected_media.push_back({track_name, TrackKind::KIND_AUDIO});
  }

  LateJoinPublicationState state;
  state.expected_publisher_identity = publisher_identity;
  for (const auto& expected : expected_media) {
    state.expected_media_tracks[expected.name] = expected.kind;
  }
  LateJoinPublicationDelegate delegate(state);
  Room consumer_room;
  consumer_room.setDelegate(&delegate);

  ASSERT_TRUE(consumer_room.connect(config_.url, config_.token_b, options)) << "Consumer failed to connect";
  ASSERT_FALSE(consumer_room.localParticipant().expired());
  ASSERT_TRUE(waitForParticipant(&consumer_room, publisher_identity, 10s))
      << "Publisher not visible to late-joining consumer";

  {
    std::unique_lock<std::mutex> lock(state.mutex);
    // Pre-existing media publications are delivered in the Connect snapshot, not as
    // TrackPublished room events. The late-joiner should still receive TrackSubscribed
    // callbacks once auto-subscribe attaches to those snapshot publications.
    const bool got_expected =
        state.cv.wait_for(lock, kWaitTimeout, [&]() { return hasExpectedMediaSubscriptions(state, expected_media); });
    EXPECT_TRUE(got_expected) << "Timed out waiting for late-join audio subscription events\n"
                              << "Published media events: " << describeMediaTracks(state.published_media_tracks) << "\n"
                              << "Subscribed media events: " << describeMediaTracks(state.subscribed_media_tracks);
  }

  std::map<std::string, TrackKind> subscribed_media_snapshot;
  std::map<std::string, int> subscribed_media_counts;
  std::vector<std::string> invariant_failures;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    subscribed_media_snapshot = state.subscribed_media_tracks;
    subscribed_media_counts = state.subscribed_media_counts;
    invariant_failures = state.invariant_failures;
  }
  EXPECT_TRUE(invariant_failures.empty()) << describeInvariantFailures(invariant_failures);

  for (const auto& expected : expected_media) {
    const auto subscribed_it = subscribed_media_snapshot.find(expected.name);
    EXPECT_NE(subscribed_it, subscribed_media_snapshot.end())
        << "Missing onTrackSubscribed event for " << expected.name
        << "; received: " << describeMediaTracks(subscribed_media_snapshot);
    if (subscribed_it != subscribed_media_snapshot.end()) {
      EXPECT_EQ(subscribed_it->second, expected.kind) << "Subscribed track kind mismatch for " << expected.name;
    }
    EXPECT_EQ(subscribed_media_counts[expected.name], 1) << "Unexpected onTrackSubscribed count for " << expected.name;
  }

  auto publisher_on_consumer = consumer_room.remoteParticipant(publisher_identity).lock();
  ASSERT_NE(publisher_on_consumer, nullptr);

  std::map<std::string, TrackKind> remote_publications;
  for (const auto& [sid, publication] : publisher_on_consumer->trackPublications()) {
    (void)sid;
    if (publication) {
      remote_publications[publication->name()] = publication->kind();
    }
  }

  for (const auto& expected : expected_media) {
    const auto it = remote_publications.find(expected.name);
    EXPECT_NE(it, remote_publications.end()) << "Late consumer snapshot missing publication " << expected.name
                                             << "; snapshot: " << describeMediaTracks(remote_publications);
    if (it != remote_publications.end()) {
      EXPECT_EQ(it->second, expected.kind) << "Snapshot track kind mismatch for " << expected.name;
    }
  }

  media_loops.stop();
  published_tracks.unpublishAll();
}

TEST_P(LateJoinTrackPublicationIntegrationTest, ConsumerReceivesAlreadyPublishedVideoTrackEvents) {
  const bool single_peer_connection = GetParam();
  RoomOptions options;
  options.auto_subscribe = true;
  options.single_peer_connection = single_peer_connection;

  Room publisher_room;
  ASSERT_TRUE(publisher_room.connect(config_.url, config_.token_a, options)) << "Publisher failed to connect";
  ASSERT_FALSE(publisher_room.localParticipant().expired());

  const std::string publisher_identity = lockLocalParticipant(publisher_room)->identity();
  ASSERT_FALSE(publisher_identity.empty());

  PublishedTrackGuard published_tracks(lockLocalParticipant(publisher_room).get());
  MediaLoopGuard media_loops;
  std::vector<ExpectedPublication> expected_media;

  for (int i = 0; i < kVideoTrackCount; ++i) {
    const std::string track_name = makeTrackName("late-join-video", i);
    auto source = std::make_shared<VideoSource>(kVideoWidth, kVideoHeight);
    auto track = LocalVideoTrack::createLocalVideoTrack(track_name, source);
    TrackPublishOptions publish_options;
    publish_options.source = TrackSource::SOURCE_CAMERA;
    publish_options.simulcast = false;

    ASSERT_NO_THROW(lockLocalParticipant(publisher_room)->publishTrack(track, publish_options));
    ASSERT_NE(track->publication(), nullptr) << "Video track was not locally published";

    published_tracks.addMediaTrack(track, track->publication()->sid());
    media_loops.addVideoSource(source, i % 2 == 1);
    expected_media.push_back({track_name, TrackKind::KIND_VIDEO});
  }

  LateJoinPublicationState state;
  state.expected_publisher_identity = publisher_identity;
  for (const auto& expected : expected_media) {
    state.expected_media_tracks[expected.name] = expected.kind;
  }
  LateJoinPublicationDelegate delegate(state);
  Room consumer_room;
  consumer_room.setDelegate(&delegate);

  ASSERT_TRUE(consumer_room.connect(config_.url, config_.token_b, options)) << "Consumer failed to connect";
  ASSERT_FALSE(consumer_room.localParticipant().expired());
  ASSERT_TRUE(waitForParticipant(&consumer_room, publisher_identity, 10s))
      << "Publisher not visible to late-joining consumer";

  {
    std::unique_lock<std::mutex> lock(state.mutex);
    // Pre-existing media publications are delivered in the Connect snapshot, not as
    // TrackPublished room events. The late-joiner should still receive TrackSubscribed
    // callbacks once auto-subscribe attaches to those snapshot publications.
    const bool got_expected =
        state.cv.wait_for(lock, kWaitTimeout, [&]() { return hasExpectedMediaSubscriptions(state, expected_media); });
    EXPECT_TRUE(got_expected) << "Timed out waiting for late-join video subscription events\n"
                              << "Published media events: " << describeMediaTracks(state.published_media_tracks) << "\n"
                              << "Subscribed media events: " << describeMediaTracks(state.subscribed_media_tracks);
  }

  std::map<std::string, TrackKind> subscribed_media_snapshot;
  std::map<std::string, int> subscribed_media_counts;
  std::vector<std::string> invariant_failures;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    subscribed_media_snapshot = state.subscribed_media_tracks;
    subscribed_media_counts = state.subscribed_media_counts;
    invariant_failures = state.invariant_failures;
  }
  EXPECT_TRUE(invariant_failures.empty()) << describeInvariantFailures(invariant_failures);

  for (const auto& expected : expected_media) {
    const auto subscribed_it = subscribed_media_snapshot.find(expected.name);
    EXPECT_NE(subscribed_it, subscribed_media_snapshot.end())
        << "Missing onTrackSubscribed event for " << expected.name
        << "; received: " << describeMediaTracks(subscribed_media_snapshot);
    if (subscribed_it != subscribed_media_snapshot.end()) {
      EXPECT_EQ(subscribed_it->second, expected.kind) << "Subscribed track kind mismatch for " << expected.name;
    }
    EXPECT_EQ(subscribed_media_counts[expected.name], 1) << "Unexpected onTrackSubscribed count for " << expected.name;
  }

  auto publisher_on_consumer = consumer_room.remoteParticipant(publisher_identity).lock();
  ASSERT_NE(publisher_on_consumer, nullptr);

  std::map<std::string, TrackKind> remote_publications;
  for (const auto& [sid, publication] : publisher_on_consumer->trackPublications()) {
    (void)sid;
    if (publication) {
      remote_publications[publication->name()] = publication->kind();
    }
  }

  for (const auto& expected : expected_media) {
    const auto it = remote_publications.find(expected.name);
    EXPECT_NE(it, remote_publications.end()) << "Late consumer snapshot missing publication " << expected.name
                                             << "; snapshot: " << describeMediaTracks(remote_publications);
    if (it != remote_publications.end()) {
      EXPECT_EQ(it->second, expected.kind) << "Snapshot track kind mismatch for " << expected.name;
    }
  }

  media_loops.stop();
  published_tracks.unpublishAll();
}

TEST_P(LateJoinTrackPublicationIntegrationTest, ConsumerReceivesAlreadyPublishedDataTrackEvents) {
  const bool single_peer_connection = GetParam();
  RoomOptions options;
  options.auto_subscribe = true;
  options.single_peer_connection = single_peer_connection;

  Room publisher_room;
  ASSERT_TRUE(publisher_room.connect(config_.url, config_.token_a, options)) << "Publisher failed to connect";
  ASSERT_FALSE(publisher_room.localParticipant().expired());

  const std::string publisher_identity = lockLocalParticipant(publisher_room)->identity();
  ASSERT_FALSE(publisher_identity.empty());

  PublishedTrackGuard published_tracks(lockLocalParticipant(publisher_room).get());
  std::set<std::string> expected_data;

  for (int i = 0; i < kDataTrackCount; ++i) {
    const std::string track_name = makeTrackName("late-join-data", i);
    auto publish_result = lockLocalParticipant(publisher_room)->publishDataTrack(track_name);
    ASSERT_TRUE(publish_result) << "Failed to publish data track " << track_name << ": "
                                << publish_result.error().message;

    const auto& track = publish_result.value();
    ASSERT_TRUE(track->isPublished()) << "Data track was not locally published: " << track_name;

    published_tracks.addDataTrack(track);
    expected_data.insert(track_name);
  }

  LateJoinPublicationState state;
  state.expected_publisher_identity = publisher_identity;
  state.expected_data_tracks = expected_data;
  LateJoinPublicationDelegate delegate(state);
  Room consumer_room;
  consumer_room.setDelegate(&delegate);

  ASSERT_TRUE(consumer_room.connect(config_.url, config_.token_b, options)) << "Consumer failed to connect";
  ASSERT_FALSE(consumer_room.localParticipant().expired());
  ASSERT_TRUE(waitForParticipant(&consumer_room, publisher_identity, 10s))
      << "Publisher not visible to late-joining consumer";

  {
    std::unique_lock<std::mutex> lock(state.mutex);
    const bool got_expected =
        state.cv.wait_for(lock, kWaitTimeout, [&]() { return hasExpectedDataPublications(state, expected_data); });
    EXPECT_TRUE(got_expected) << "Timed out waiting for late-join data publication events\n"
                              << "Published data events: " << describeDataTracks(state.published_data_tracks);
  }

  std::map<std::string, std::string> data_snapshot;
  std::map<std::string, int> data_counts;
  std::vector<std::string> invariant_failures;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    data_snapshot = state.published_data_tracks;
    data_counts = state.published_data_counts;
    invariant_failures = state.invariant_failures;
  }
  EXPECT_TRUE(invariant_failures.empty()) << describeInvariantFailures(invariant_failures);

  EXPECT_EQ(data_snapshot.size(), expected_data.size())
      << "Late-joining consumer received an unexpected number of data track publication callbacks: "
      << describeDataTracks(data_snapshot);

  for (const auto& name : expected_data) {
    const auto it = data_snapshot.find(name);
    EXPECT_NE(it, data_snapshot.end()) << "Missing onDataTrackPublished event for " << name;
    if (it != data_snapshot.end()) {
      EXPECT_EQ(it->second, publisher_identity) << "Publisher identity mismatch for data track " << name;
    }
    EXPECT_EQ(data_counts[name], 1) << "Unexpected onDataTrackPublished count for " << name;
  }

  published_tracks.unpublishAll();
}

INSTANTIATE_TEST_SUITE_P(PeerConnectionModes, LateJoinTrackPublicationIntegrationTest, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) {
                           return info.param ? "SinglePeerConnection" : "DualPeerConnection";
                         });

} // namespace livekit::test
