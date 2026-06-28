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
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "../common/test_common.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr auto kSubscriptionTimeout = 20s;

/// Tracks subscription/unpublish events seen by the receiver so tests can wait
/// for the platform-audio track to round-trip through the SFU.
struct PlatformTrackState {
  std::mutex mutex;
  std::condition_variable cv;
  std::set<std::string> subscribed_audio_names;
  std::set<std::string> unsubscribed_sids;
  std::set<std::string> unpublished_sids;
};

class PlatformTrackCollectorDelegate : public RoomDelegate {
public:
  explicit PlatformTrackCollectorDelegate(PlatformTrackState& state) : state_(state) {}

  void onTrackSubscribed(Room&, const TrackSubscribedEvent& event) override {
    std::lock_guard<std::mutex> lock(state_.mutex);
    if (event.track && event.track->kind() == TrackKind::KIND_AUDIO && event.publication) {
      state_.subscribed_audio_names.insert(event.publication->name());
    }
    state_.cv.notify_all();
  }

  void onTrackUnsubscribed(Room&, const TrackUnsubscribedEvent& event) override {
    std::lock_guard<std::mutex> lock(state_.mutex);
    if (event.track) {
      state_.unsubscribed_sids.insert(event.track->sid());
    }
    state_.cv.notify_all();
  }

  void onTrackUnpublished(Room&, const TrackUnpublishedEvent& event) override {
    std::lock_guard<std::mutex> lock(state_.mutex);
    if (event.publication) {
      state_.unpublished_sids.insert(event.publication->sid());
    }
    state_.cv.notify_all();
  }

private:
  PlatformTrackState& state_;
};

} // namespace

class PlatformAudioIntegrationTest : public LiveKitTestBase {};

// Publishing a platform-ADM-backed audio track should reach a remote
// participant exactly like a manually fed AudioSource track. Unlike AudioSource,
// PlatformAudio requires a working platform Audio Device Module: constructing it
// throws PlatformAudioError when no ADM is available (e.g. a headless runner with
// no audio subsystem), so the test is skipped in that environment.
TEST_F(PlatformAudioIntegrationTest, PublishPlatformAudioTrackEndToEnd) {
  EXPECT_TRUE(config_.available) << "Missing integration configuration";

  std::unique_ptr<PlatformAudio> platform_audio;
  try {
    platform_audio = std::make_unique<PlatformAudio>();
  } catch (const PlatformAudioError& error) {
    GTEST_SKIP() << "PlatformAudio unavailable: " << error.what();
  }

  RoomOptions options;
  options.auto_subscribe = true;

  PlatformTrackState receiver_state;
  PlatformTrackCollectorDelegate receiver_delegate(receiver_state);

  auto receiver_room = std::make_unique<Room>();
  receiver_room->setDelegate(&receiver_delegate);
  ASSERT_TRUE(receiver_room->connect(config_.url, config_.token_b, options)) << "Receiver failed to connect";

  auto sender_room = std::make_unique<Room>();
  ASSERT_TRUE(sender_room->connect(config_.url, config_.token_a, options)) << "Sender failed to connect";

  const auto source = platform_audio->createAudioSource();
  ASSERT_NE(source, nullptr);
  EXPECT_NE(source->ffiHandleId(), 0u);

  const std::string track_name = "platform-mic";
  const auto track = LocalAudioTrack::createLocalAudioTrack(track_name, source);
  ASSERT_NE(track, nullptr);

  TrackPublishOptions publish_options;
  publish_options.source = TrackSource::SOURCE_MICROPHONE;
  lockLocalParticipant(*sender_room)->publishTrack(track, publish_options);

  std::unique_lock<std::mutex> lock(receiver_state.mutex);
  const bool subscribed = receiver_state.cv.wait_for(
      lock, kSubscriptionTimeout, [&]() { return receiver_state.subscribed_audio_names.count(track_name) > 0; });
  EXPECT_TRUE(subscribed) << "Receiver never subscribed to the platform audio track";
}

// Unpublishing a platform audio track must propagate to the remote, exercising
// the source/track lifecycle that keeps the shared PlatformAudioState alive.
TEST_F(PlatformAudioIntegrationTest, UnpublishPlatformAudioTrackPropagates) {
  EXPECT_TRUE(config_.available) << "Missing integration configuration";

  std::unique_ptr<PlatformAudio> platform_audio;
  try {
    platform_audio = std::make_unique<PlatformAudio>();
  } catch (const PlatformAudioError& error) {
    GTEST_SKIP() << "PlatformAudio unavailable: " << error.what();
  }

  RoomOptions options;
  options.auto_subscribe = true;

  PlatformTrackState receiver_state;
  PlatformTrackCollectorDelegate receiver_delegate(receiver_state);

  auto receiver_room = std::make_unique<Room>();
  receiver_room->setDelegate(&receiver_delegate);
  ASSERT_TRUE(receiver_room->connect(config_.url, config_.token_b, options)) << "Receiver failed to connect";

  auto sender_room = std::make_unique<Room>();
  ASSERT_TRUE(sender_room->connect(config_.url, config_.token_a, options)) << "Sender failed to connect";

  const auto source = platform_audio->createAudioSource();
  ASSERT_NE(source, nullptr);

  const std::string track_name = "platform-mic-unpublish";
  const auto track = LocalAudioTrack::createLocalAudioTrack(track_name, source);
  ASSERT_NE(track, nullptr);

  TrackPublishOptions publish_options;
  publish_options.source = TrackSource::SOURCE_MICROPHONE;
  lockLocalParticipant(*sender_room)->publishTrack(track, publish_options);

  {
    std::unique_lock<std::mutex> lock(receiver_state.mutex);
    ASSERT_TRUE(receiver_state.cv.wait_for(lock, kSubscriptionTimeout, [&]() {
      return receiver_state.subscribed_audio_names.count(track_name) > 0;
    })) << "Receiver never subscribed to the platform audio track";
  }

  ASSERT_NE(track->publication(), nullptr);
  const std::string published_sid = track->publication()->sid();
  lockLocalParticipant(*sender_room)->unpublishTrack(published_sid);

  std::unique_lock<std::mutex> lock(receiver_state.mutex);
  const bool removed = receiver_state.cv.wait_for(lock, kSubscriptionTimeout, [&]() {
    return receiver_state.unpublished_sids.count(published_sid) > 0 ||
           receiver_state.unsubscribed_sids.count(published_sid) > 0;
  });
  EXPECT_TRUE(removed) << "Receiver never saw the platform audio track removed";
}

// A single PlatformAudio manager can vend multiple independent sources, each
// with a distinct FFI handle, and both should publish end-to-end.
TEST_F(PlatformAudioIntegrationTest, MultipleSourcesFromOneManagerPublish) {
  EXPECT_TRUE(config_.available) << "Missing integration configuration";

  std::unique_ptr<PlatformAudio> platform_audio;
  try {
    platform_audio = std::make_unique<PlatformAudio>();
  } catch (const PlatformAudioError& error) {
    GTEST_SKIP() << "PlatformAudio unavailable: " << error.what();
  }

  RoomOptions options;
  options.auto_subscribe = true;

  PlatformTrackState receiver_state;
  PlatformTrackCollectorDelegate receiver_delegate(receiver_state);

  auto receiver_room = std::make_unique<Room>();
  receiver_room->setDelegate(&receiver_delegate);
  ASSERT_TRUE(receiver_room->connect(config_.url, config_.token_b, options)) << "Receiver failed to connect";

  auto sender_room = std::make_unique<Room>();
  ASSERT_TRUE(sender_room->connect(config_.url, config_.token_a, options)) << "Sender failed to connect";

  const auto source_a = platform_audio->createAudioSource();
  const auto source_b = platform_audio->createAudioSource();
  ASSERT_NE(source_a, nullptr);
  ASSERT_NE(source_b, nullptr);
  EXPECT_NE(source_a->ffiHandleId(), 0u);
  EXPECT_NE(source_b->ffiHandleId(), 0u);
  EXPECT_NE(source_a->ffiHandleId(), source_b->ffiHandleId());

  const std::string name_a = "platform-mic-a";
  const std::string name_b = "platform-mic-b";
  const auto track_a = LocalAudioTrack::createLocalAudioTrack(name_a, source_a);
  const auto track_b = LocalAudioTrack::createLocalAudioTrack(name_b, source_b);
  ASSERT_NE(track_a, nullptr);
  ASSERT_NE(track_b, nullptr);

  TrackPublishOptions publish_options;
  publish_options.source = TrackSource::SOURCE_MICROPHONE;
  lockLocalParticipant(*sender_room)->publishTrack(track_a, publish_options);
  lockLocalParticipant(*sender_room)->publishTrack(track_b, publish_options);

  std::unique_lock<std::mutex> lock(receiver_state.mutex);
  const bool both_subscribed = receiver_state.cv.wait_for(lock, kSubscriptionTimeout, [&]() {
    return receiver_state.subscribed_audio_names.count(name_a) > 0 &&
           receiver_state.subscribed_audio_names.count(name_b) > 0;
  });
  EXPECT_TRUE(both_subscribed) << "Receiver did not subscribe to both platform audio tracks";
}

// Audio captured by the platform Audio Device Module must actually stream to a
// remote participant as decoded frames, not merely produce a subscribed track.
// PlatformAudioSource captures the real microphone, so this verifies frames
// *flow* end-to-end without asserting on their content. PlatformAudio requires a
// working platform ADM, so the test is skipped when one is unavailable (e.g. a
// headless runner with no audio subsystem).
TEST_F(PlatformAudioIntegrationTest, PlatformAudioFramesReachRemote) {
  EXPECT_TRUE(config_.available) << "Missing integration configuration";

  std::unique_ptr<PlatformAudio> platform_audio;
  try {
    platform_audio = std::make_unique<PlatformAudio>();
  } catch (const PlatformAudioError& error) {
    GTEST_SKIP() << "PlatformAudio unavailable: " << error.what();
  }

  // Some platforms (notably Windows) construct a valid ADM even on a headless
  // CI runner with no microphone: guard against that here.
  if (platform_audio->recordingDeviceCount() == 0) {
    GTEST_SKIP() << "No recording device available; cannot capture platform audio frames";
  }

  RoomOptions options;
  options.auto_subscribe = true;

  PlatformTrackState receiver_state;
  PlatformTrackCollectorDelegate receiver_delegate(receiver_state);

  auto receiver_room = std::make_unique<Room>();
  receiver_room->setDelegate(&receiver_delegate);
  ASSERT_TRUE(receiver_room->connect(config_.url, config_.token_b, options)) << "Receiver failed to connect";

  auto sender_room = std::make_unique<Room>();
  ASSERT_TRUE(sender_room->connect(config_.url, config_.token_a, options)) << "Sender failed to connect";

  const std::string sender_identity = lockLocalParticipant(*sender_room)->identity();

  const auto source = platform_audio->createAudioSource();
  ASSERT_NE(source, nullptr);

  const std::string track_name = "platform-mic-frames";
  const auto track = LocalAudioTrack::createLocalAudioTrack(track_name, source);
  ASSERT_NE(track, nullptr);

  // A few hundred ms of audio (10ms frames) is plenty to confirm the media path
  // is live without making the test slow.
  constexpr int kRequiredFrames = 10;
  constexpr auto kFrameTimeout = 20s;

  std::mutex frame_mutex;
  std::condition_variable frame_cv;
  int received_frames = 0;

  // The reader thread is only started when the subscription event fires and a
  // matching callback is already registered, so register before publishing.
  receiver_room->setOnAudioFrameCallback(sender_identity, track_name, [&](const AudioFrame& frame) {
    if (frame.totalSamples() == 0) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(frame_mutex);
      ++received_frames;
    }
    frame_cv.notify_all();
  });

  TrackPublishOptions publish_options;
  publish_options.source = TrackSource::SOURCE_MICROPHONE;
  lockLocalParticipant(*sender_room)->publishTrack(track, publish_options);

  {
    std::unique_lock<std::mutex> lock(receiver_state.mutex);
    ASSERT_TRUE(receiver_state.cv.wait_for(lock, kSubscriptionTimeout, [&]() {
      return receiver_state.subscribed_audio_names.count(track_name) > 0;
    })) << "Receiver never subscribed to the platform audio track";
  }

  bool frames_received = false;
  {
    std::unique_lock<std::mutex> lock(frame_mutex);
    frames_received = frame_cv.wait_for(lock, kFrameTimeout, [&]() { return received_frames >= kRequiredFrames; });
  }
  EXPECT_TRUE(frames_received) << "Receiver did not get platform audio frames from the remote";

  receiver_room->clearOnAudioFrameCallback(sender_identity, track_name);
}

namespace {

/// Run one publish/subscribe/unpublish cycle against a fresh pair of rooms,
/// reusing a caller-owned PlatformAudio so the underlying Rust LkRuntime (and
/// therefore the platform Audio Device Module) is never torn down between
/// cycles. Returns true if the receiver observed the published track.
bool runPlatformAudioCycle(PlatformAudio& platform_audio, const TestConfig& config, const std::string& track_name) {
  RoomOptions options;
  options.auto_subscribe = true;

  PlatformTrackState receiver_state;
  PlatformTrackCollectorDelegate receiver_delegate(receiver_state);

  auto receiver_room = std::make_unique<Room>();
  receiver_room->setDelegate(&receiver_delegate);
  if (!receiver_room->connect(config.url, config.token_b, options)) {
    return false;
  }

  auto sender_room = std::make_unique<Room>();
  if (!sender_room->connect(config.url, config.token_a, options)) {
    return false;
  }

  const auto source = platform_audio.createAudioSource();
  if (source == nullptr) {
    return false;
  }

  const auto track = LocalAudioTrack::createLocalAudioTrack(track_name, source);
  if (track == nullptr) {
    return false;
  }

  TrackPublishOptions publish_options;
  publish_options.source = TrackSource::SOURCE_MICROPHONE;
  lockLocalParticipant(*sender_room)->publishTrack(track, publish_options);

  std::unique_lock<std::mutex> lock(receiver_state.mutex);
  return receiver_state.cv.wait_for(lock, kSubscriptionTimeout,
                                    [&]() { return receiver_state.subscribed_audio_names.count(track_name) > 0; });
}

} // namespace

// Control arm for the macOS PlatformAudio instability investigation.
//
// The standard PlatformAudioIntegrationTest cases each call livekit::shutdown()
// in TearDown(), which disposes the FFI server, drops the last Arc<LkRuntime>,
// and runs AdmProxy::~AdmProxy() -> platform_adm_->Terminate(). Under
// --gtest_repeat that means the native CoreAudio ADM is fully terminated and
// recreated on *every* iteration -- the suspected crash path.
//
// This test instead holds a single PlatformAudio alive for the whole test, so
// the runtime and ADM are created once and never terminated between cycles. It
// loops the same connect/publish/subscribe cycle PLATFORM_AUDIO_PIN_ITERATIONS
// times (default 20). If the repeat arm crashes on macOS but this pinned arm
// stays green, the instability is in ADM teardown/recreation, not the steady
// media path.
TEST_F(PlatformAudioIntegrationTest, PinnedRuntimeRepeatedPublishStress) {
  EXPECT_TRUE(config_.available) << "Missing integration configuration";

  std::unique_ptr<PlatformAudio> platform_audio;
  try {
    platform_audio = std::make_unique<PlatformAudio>();
  } catch (const PlatformAudioError& error) {
    GTEST_SKIP() << "PlatformAudio unavailable: " << error.what();
  }

  int iterations = 20;
  if (const char* env = std::getenv("PLATFORM_AUDIO_PIN_ITERATIONS")) {
    const int parsed = std::atoi(env);
    if (parsed > 0) {
      iterations = parsed;
    }
  }

  for (int i = 0; i < iterations; ++i) {
    const std::string track_name = "platform-mic-pinned-" + std::to_string(i);
    const bool subscribed = runPlatformAudioCycle(*platform_audio, config_, track_name);
    ASSERT_TRUE(subscribed) << "Receiver never subscribed on pinned iteration " << i;
  }
}

} // namespace livekit::test
