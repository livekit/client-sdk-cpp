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

#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "../common/test_common.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr auto kSubscriptionTimeout = 20s;

struct PlatformTrackState {
  std::mutex mutex;
  std::condition_variable cv;
  std::set<std::string> subscribed_audio_names;
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

private:
  PlatformTrackState& state_;
};

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

class PlatformAudioStressTest : public LiveKitTestBase {};

// Control arm for the macOS PlatformAudio instability investigation.
//
// The standard PlatformAudioIntegrationTest cases each call livekit::shutdown()
// in TearDown(), which disposes the FFI server, drops the last Arc<LkRuntime>,
// and runs AdmProxy::~AdmProxy() -> platform_adm_->Terminate(). Under
// --gtest_repeat that means the native CoreAudio ADM is fully terminated and
// recreated on every iteration -- the suspected crash path.
//
// This test instead holds a single PlatformAudio alive for the whole test, so
// the runtime and ADM are created once and never terminated between cycles.
TEST_F(PlatformAudioStressTest, PinnedRuntimeRepeatedPublishStress) {
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
