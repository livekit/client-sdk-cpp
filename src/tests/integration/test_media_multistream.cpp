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

#include "../common/test_common.h"
#include <atomic>
#include <chrono>
#include <cmath>
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

constexpr int kVideoWidth = 640;
constexpr int kVideoHeight = 360;
constexpr int kAudioSampleRate = 48000;
constexpr int kAudioChannels = 1;
constexpr int kAudioFrameMs = 10;
constexpr int kSamplesPerChannel = kAudioSampleRate * kAudioFrameMs / 1000;

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

void fillWebcamLikeFrame(VideoFrame &frame, std::uint64_t frame_index) {
  // ARGB layout: [A, R, G, B]
  std::uint8_t *data = frame.data();
  const std::size_t size = frame.dataSize();
  const std::uint8_t blue = static_cast<std::uint8_t>((frame_index * 3) % 255);
  for (std::size_t i = 0; i < size; i += 4) {
    data[i + 0] = 255; // A
    data[i + 1] = 0;   // R
    data[i + 2] = 170; // G
    data[i + 3] = blue;
  }
}

void fillRedFrameWithMetadata(VideoFrame &frame, std::uint64_t frame_index,
                              std::uint64_t timestamp_us) {
  // ARGB layout: [A, R, G, B]
  std::uint8_t *data = frame.data();
  const std::size_t size = frame.dataSize();
  for (std::size_t i = 0; i < size; i += 4) {
    data[i + 0] = 255; // A
    data[i + 1] = 255; // R
    data[i + 2] = 0;   // G
    data[i + 3] = 0;   // B
  }

  // Encode frame counter + timestamp into first 16 pixels for easy debugging.
  std::uint8_t meta[16];
  for (int i = 0; i < 8; ++i) {
    meta[i] = static_cast<std::uint8_t>((frame_index >> (i * 8)) & 0xFF);
    meta[8 + i] = static_cast<std::uint8_t>((timestamp_us >> (i * 8)) & 0xFF);
  }
  for (int i = 0; i < 16; ++i) {
    const std::size_t px = static_cast<std::size_t>(i) * 4;
    if (px + 3 < size) {
      data[px + 0] = 255;
      data[px + 1] = 255;
      data[px + 2] = meta[i];
      data[px + 3] = meta[(15 - i)];
    }
  }
}

void runVideoLoop(const std::shared_ptr<VideoSource> &source,
                  std::atomic<bool> &running,
                  void (*fill_fn)(VideoFrame &, std::uint64_t, std::uint64_t)) {
  VideoFrame frame =
      VideoFrame::create(kVideoWidth, kVideoHeight, VideoBufferType::ARGB);
  std::uint64_t frame_index = 0;
  while (running.load(std::memory_order_relaxed)) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ts_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
    fill_fn(frame, frame_index, ts_us);
    try {
      source->captureFrame(frame, static_cast<std::int64_t>(ts_us),
                           VideoRotation::VIDEO_ROTATION_0);
    } catch (...) {
      break;
    }
    frame_index++;
    std::this_thread::sleep_for(33ms);
  }
}

void fillWebcamWrapper(VideoFrame &frame, std::uint64_t frame_index,
                       std::uint64_t) {
  fillWebcamLikeFrame(frame, frame_index);
}

void fillRedWrapper(VideoFrame &frame, std::uint64_t frame_index,
                    std::uint64_t timestamp_us) {
  fillRedFrameWithMetadata(frame, frame_index, timestamp_us);
}

void runToneLoop(const std::shared_ptr<AudioSource> &source,
                 std::atomic<bool> &running, double base_freq_hz,
                 bool siren_mode) {
  double phase = 0.0;
  constexpr double kTwoPi = 6.283185307179586;
  while (running.load(std::memory_order_relaxed)) {
    AudioFrame frame = AudioFrame::create(kAudioSampleRate, kAudioChannels,
                                          kSamplesPerChannel);
    auto &samples = frame.data();

    const double time_sec =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count()) /
        1000.0;
    const double freq =
        siren_mode ? (700.0 + 250.0 * std::sin(time_sec * 2.0)) : base_freq_hz;

    const double phase_inc =
        kTwoPi * freq / static_cast<double>(kAudioSampleRate);
    for (int i = 0; i < kSamplesPerChannel; ++i) {
      samples[static_cast<std::size_t>(i)] =
          static_cast<std::int16_t>(std::sin(phase) * 12000.0);
      phase += phase_inc;
      if (phase > kTwoPi) {
        phase -= kTwoPi;
      }
    }

    try {
      source->captureFrame(frame);
    } catch (...) {
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(kAudioFrameMs));
  }
}

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
  std::vector<std::shared_ptr<LocalTrackPublication>> video_pubs;
  std::vector<std::shared_ptr<AudioSource>> audio_sources;
  std::vector<std::shared_ptr<LocalAudioTrack>> audio_tracks;
  std::vector<std::shared_ptr<LocalTrackPublication>> audio_pubs;
  std::vector<std::thread> threads;
  std::set<std::string> expected_names;

  video_sources.reserve(kVideoTrackCount);
  video_tracks.reserve(kVideoTrackCount);
  video_pubs.reserve(kVideoTrackCount);
  audio_sources.reserve(kAudioTrackCount);
  audio_tracks.reserve(kAudioTrackCount);
  audio_pubs.reserve(kAudioTrackCount);
  threads.reserve(kVideoTrackCount + kAudioTrackCount);

  for (int i = 0; i < kVideoTrackCount; ++i) {
    auto source = std::make_shared<VideoSource>(kVideoWidth, kVideoHeight);
    const std::string name = "video-track-" + std::to_string(i);
    auto track = LocalVideoTrack::createLocalVideoTrack(name, source);
    TrackPublishOptions opts;
    opts.source = (i % 2 == 0) ? TrackSource::SOURCE_CAMERA
                               : TrackSource::SOURCE_SCREENSHARE;
    auto pub = sender_room->localParticipant()->publishTrack(track, opts);
    std::cerr << "[MediaMultiStream] published video " << name
              << " sid=" << pub->sid() << std::endl;
    video_sources.push_back(source);
    video_tracks.push_back(track);
    video_pubs.push_back(pub);
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
    auto pub = sender_room->localParticipant()->publishTrack(track, opts);
    std::cerr << "[MediaMultiStream] published audio " << name
              << " sid=" << pub->sid() << std::endl;
    audio_sources.push_back(source);
    audio_tracks.push_back(track);
    audio_pubs.push_back(pub);
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

  for (const auto &pub : video_pubs) {
    sender_room->localParticipant()->unpublishTrack(pub->sid());
  }
  for (const auto &pub : audio_pubs) {
    sender_room->localParticipant()->unpublishTrack(pub->sid());
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
