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

#include "../common/audio_utils.h"
#include "../common/test_common.h"
#include "../common/video_utils.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace livekit {
namespace test {

using namespace std::chrono_literals;

namespace {

constexpr int kVideoWidth = kDefaultVideoWidth;
constexpr int kVideoHeight = kDefaultVideoHeight;
constexpr int kAudioSampleRate = kDefaultAudioSampleRate;
constexpr int kAudioChannels = kDefaultAudioChannels;

struct MediaSubscriptionState {
  std::mutex mutex;
  std::condition_variable cv;
  std::set<std::string> subscribed_track_names;
  int audio_tracks = 0;
  int video_tracks = 0;
};

class MediaTrackCollectorDelegate : public RoomDelegate {
public:
  explicit MediaTrackCollectorDelegate(MediaSubscriptionState &state)
      : state_(state) {}

  void onTrackSubscribed(Room &, const TrackSubscribedEvent &event) override {
    std::lock_guard<std::mutex> lock(state_.mutex);
    std::string name = "<unknown>";
    std::string sid = "<unknown>";
    if (event.track) {
      if (event.track->kind() == TrackKind::KIND_AUDIO) {
        state_.audio_tracks++;
      } else if (event.track->kind() == TrackKind::KIND_VIDEO) {
        state_.video_tracks++;
      }
      sid = event.track->sid();
    }
    if (event.publication) {
      name = event.publication->name();
      state_.subscribed_track_names.insert(name);
    }
    std::cerr << "[MediaMultiStream] onTrackSubscribed name=" << name
              << " sid=" << sid << " audio_count=" << state_.audio_tracks
              << " video_count=" << state_.video_tracks << std::endl;
    state_.cv.notify_all();
  }

private:
  MediaSubscriptionState &state_;
};

} // namespace

class MediaMultiStreamIntegrationTest : public LiveKitTestBase {
protected:
  void runPublishTwoVideoAndTwoAudioTracks(bool single_peer_connection);
};

void MediaMultiStreamIntegrationTest::runPublishTwoVideoAndTwoAudioTracks(
    bool single_peer_connection) {
  if (!config_.available) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                    "LIVEKIT_RECEIVER_TOKEN not set";
  }

  RoomOptions options;
  options.auto_subscribe = true;
  options.single_peer_connection = single_peer_connection;

  MediaSubscriptionState receiver_state;
  MediaTrackCollectorDelegate receiver_delegate(receiver_state);

  auto receiver_room = std::make_unique<Room>();
  receiver_room->setDelegate(&receiver_delegate);
  ASSERT_TRUE(
      receiver_room->Connect(config_.url, config_.receiver_token, options))
      << "Receiver failed to connect";

  auto sender_room = std::make_unique<Room>();
  ASSERT_TRUE(sender_room->Connect(config_.url, config_.caller_token, options))
      << "Sender failed to connect";

  const std::string receiver_identity =
      receiver_room->localParticipant()->identity();
  const std::string sender_identity =
      sender_room->localParticipant()->identity();

  constexpr int kVideoTrackCount = 10;
  constexpr int kAudioTrackCount = 10;

  std::vector<std::shared_ptr<VideoSource>> video_sources;
  std::vector<std::shared_ptr<LocalVideoTrack>> video_tracks;
  std::vector<std::shared_ptr<AudioSource>> audio_sources;
  std::vector<std::shared_ptr<LocalAudioTrack>> audio_tracks;
  std::vector<std::thread> threads;
  std::set<std::string> expected_names;

  video_sources.reserve(kVideoTrackCount);
  video_tracks.reserve(kVideoTrackCount);
  audio_sources.reserve(kAudioTrackCount);
  audio_tracks.reserve(kAudioTrackCount);
  threads.reserve(kVideoTrackCount + kAudioTrackCount);

  for (int i = 0; i < kVideoTrackCount; ++i) {
    auto source = std::make_shared<VideoSource>(kVideoWidth, kVideoHeight);
    const std::string name = "video-track-" + std::to_string(i);
    auto track = LocalVideoTrack::createLocalVideoTrack(name, source);
    TrackPublishOptions opts;
    opts.source = (i % 2 == 0) ? TrackSource::SOURCE_CAMERA
                               : TrackSource::SOURCE_SCREENSHARE;
    sender_room->localParticipant()->publishTrack(track, opts);
    std::cerr << "[MediaMultiStream] published video " << name
              << " sid=" << track->sid() << std::endl;
    video_sources.push_back(source);
    video_tracks.push_back(track);
    expected_names.insert(name);
  }

  for (int i = 0; i < kAudioTrackCount; ++i) {
    auto source =
        std::make_shared<AudioSource>(kAudioSampleRate, kAudioChannels, 0);
    const std::string name = "audio-track-" + std::to_string(i);
    auto track = LocalAudioTrack::createLocalAudioTrack(name, source);
    TrackPublishOptions opts;
    opts.source = (i % 2 == 0) ? TrackSource::SOURCE_MICROPHONE
                               : TrackSource::SOURCE_SCREENSHARE_AUDIO;
    sender_room->localParticipant()->publishTrack(track, opts);
    std::cerr << "[MediaMultiStream] published audio " << name
              << " sid=" << track->sid() << std::endl;
    audio_sources.push_back(source);
    audio_tracks.push_back(track);
    expected_names.insert(name);
  }

  std::atomic<bool> running{true};
  for (int i = 0; i < kVideoTrackCount; ++i) {
    auto source = video_sources[static_cast<std::size_t>(i)];
    const bool red_mode = (i % 2 == 1);
    threads.emplace_back([source, &running, red_mode]() {
      runVideoLoop(source, running,
                   red_mode ? fillRedWrapper : fillWebcamWrapper);
    });
  }
  for (int i = 0; i < kAudioTrackCount; ++i) {
    auto source = audio_sources[static_cast<std::size_t>(i)];
    const bool siren_mode = (i % 2 == 1);
    const double base_freq = 320.0 + static_cast<double>(i) * 40.0;
    threads.emplace_back([source, &running, base_freq, siren_mode]() {
      runToneLoop(source, running, base_freq, siren_mode);
    });
  }

  {
    std::unique_lock<std::mutex> lock(receiver_state.mutex);
    const bool all_received = receiver_state.cv.wait_for(lock, 20s, [&]() {
      return receiver_state.subscribed_track_names.size() >=
             expected_names.size();
    });
    EXPECT_TRUE(all_received) << "Timed out waiting for all subscribed tracks";
    if (!all_received) {
      std::cerr << "[MediaMultiStream] timeout waiting subscriptions; received "
                   "names:";
      for (const auto &n : receiver_state.subscribed_track_names) {
        std::cerr << " " << n;
      }
      std::cerr << " (audio=" << receiver_state.audio_tracks
                << " video=" << receiver_state.video_tracks << ")" << std::endl;
    }
  }

  {
    std::lock_guard<std::mutex> lock(receiver_state.mutex);
    for (const auto &expected_name : expected_names) {
      EXPECT_TRUE(receiver_state.subscribed_track_names.count(expected_name) >
                  0)
          << "Missing subscribed track: " << expected_name;
    }
    EXPECT_GE(receiver_state.video_tracks, kVideoTrackCount);
    EXPECT_GE(receiver_state.audio_tracks, kAudioTrackCount);
  }

  auto *sender_on_receiver = receiver_room->remoteParticipant(sender_identity);
  ASSERT_NE(sender_on_receiver, nullptr);
  std::cerr << "[MediaMultiStream] receiver sees sender publications="
            << sender_on_receiver->trackPublications().size() << std::endl;
  for (const auto &kv : sender_on_receiver->trackPublications()) {
    const auto &pub = kv.second;
    std::cerr << "[MediaMultiStream] remote publication sid=" << kv.first
              << " name=" << (pub ? pub->name() : "<null>") << " kind="
              << (pub && pub->kind() == TrackKind::KIND_AUDIO ? "audio"
                                                              : "video")
              << " source=" << (pub ? static_cast<int>(pub->source()) : -1)
              << std::endl;
  }
  EXPECT_GE(sender_on_receiver->trackPublications().size(),
            static_cast<std::size_t>(kVideoTrackCount + kAudioTrackCount));

  running.store(false, std::memory_order_relaxed);
  for (auto &t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }

  for (const auto &track : video_tracks) {
    sender_room->localParticipant()->unpublishTrack(track->sid());
  }
  for (const auto &track : audio_tracks) {
    sender_room->localParticipant()->unpublishTrack(track->sid());
  }
}

TEST_F(MediaMultiStreamIntegrationTest,
       PublishTwoVideoAndTwoAudioTracks_DualPeerConnection) {
  runPublishTwoVideoAndTwoAudioTracks(false);
}

TEST_F(MediaMultiStreamIntegrationTest,
       PublishTwoVideoAndTwoAudioTracks_SinglePeerConnection) {
  runPublishTwoVideoAndTwoAudioTracks(true);
}

} // namespace test
} // namespace livekit
