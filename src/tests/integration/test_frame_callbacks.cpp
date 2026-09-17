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

/// @file test_frame_callbacks.cpp
/// @brief Regression coverage for GitHub issue #235.
///
/// A frame callback registered *after* the track_subscribed event -- from a
/// GUI thread, say, once RoomDelegate::onTrackSubscribed has already returned
/// -- used to register fine but never start a reader thread, so the callback
/// was never invoked. Registration must start a reader from the retained
/// subscription regardless of which side of the event it lands on, and
/// regardless of whether the track is audio or video.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "tests/common/audio_utils.h"
#include "tests/common/room_test_access.h"
#include "tests/common/test_common.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr auto kSubscribeTimeout = 15s;
constexpr auto kFrameTimeout = 15s;
constexpr int kFrameWidth = 16;
constexpr int kFrameHeight = 16;

template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return predicate();
}

/// Wait until @p room reports a subscribed track named @p track_name of @p kind
/// published by @p identity. This is the state the issue describes: the
/// subscription event has been fully processed and the delegate has returned.
bool waitForSubscribedTrack(Room& room, const std::string& identity, const std::string& track_name, TrackKind kind,
                            std::chrono::milliseconds timeout) {
  return waitFor(
      [&]() {
        auto participant = room.remoteParticipant(identity).lock();
        if (participant == nullptr) {
          return false;
        }
        for (const auto& [sid, publication] : participant->trackPublications()) {
          (void)sid;
          if (publication == nullptr || publication->name() != track_name || publication->kind() != kind) {
            continue;
          }
          if (publication->subscribed() && publication->track() != nullptr) {
            return true;
          }
        }
        return false;
      },
      timeout);
}

/// Run @p action on a freshly spawned thread and wait for it -- the shape of
/// the original report, where registration came from a GUI thread rather than
/// from the FFI event thread that delivers onTrackSubscribed.
void registerFromAnotherThread(const std::function<void()>& action) {
  std::thread registrar(action);
  registrar.join();
}

/// Two connected rooms. The receiver has no frame callback registered until a
/// test explicitly asks for one; the sender can publish one video and one audio
/// track and keeps them fed until stopped.
class LateRegistrationFixture {
public:
  LateRegistrationFixture(const std::string& url, const std::string& token_a, const std::string& token_b) {
    RoomOptions options;
    options.auto_subscribe = true;
    connected_ = receiver_.connect(url, token_b, options) && sender_.connect(url, token_a, options);
    if (!connected_) {
      return;
    }
    if (sender_.localParticipant().expired() || receiver_.localParticipant().expired()) {
      connected_ = false;
      return;
    }
    sender_identity_ = lockLocalParticipant(sender_)->identity();
    connected_ = waitForParticipant(&receiver_, sender_identity_, kSubscribeTimeout);
  }

  LateRegistrationFixture(const LateRegistrationFixture&) = delete;
  LateRegistrationFixture& operator=(const LateRegistrationFixture&) = delete;

  ~LateRegistrationFixture() { stop(); }

  bool connected() const { return connected_; }
  Room& receiver() { return receiver_; }
  Room& sender() { return sender_; }
  const std::string& senderIdentity() const { return sender_identity_; }

  /// Publish a video track and block until the receiver reports it subscribed.
  bool publishVideoAndAwaitSubscription(const std::string& track_name) {
    video_source_ = std::make_shared<VideoSource>(kFrameWidth, kFrameHeight);
    video_track_ = LocalVideoTrack::createLocalVideoTrack(track_name, video_source_);

    TrackPublishOptions publish_options;
    publish_options.source = TrackSource::SOURCE_CAMERA;
    publish_options.simulcast = false;
    lockLocalParticipant(sender_)->publishTrack(video_track_, publish_options);

    video_thread_ = std::thread([this]() {
      VideoFrame frame = VideoFrame::create(kFrameWidth, kFrameHeight, VideoBufferType::RGBA);
      std::fill(frame.data(), frame.data() + frame.dataSize(), 0x7f);
      while (running_.load(std::memory_order_relaxed)) {
        try {
          video_source_->captureFrame(frame);
        } catch (...) {
          break;
        }
        std::this_thread::sleep_for(50ms);
      }
    });

    return waitForSubscribedTrack(receiver_, sender_identity_, track_name, TrackKind::KIND_VIDEO, kSubscribeTimeout);
  }

  /// Publish an audio track and block until the receiver reports it subscribed.
  bool publishAudioAndAwaitSubscription(const std::string& track_name) {
    audio_source_ = std::make_shared<AudioSource>(kDefaultAudioSampleRate, kDefaultAudioChannels);
    audio_track_ = LocalAudioTrack::createLocalAudioTrack(track_name, audio_source_);

    TrackPublishOptions publish_options;
    publish_options.source = TrackSource::SOURCE_MICROPHONE;
    lockLocalParticipant(sender_)->publishTrack(audio_track_, publish_options);

    audio_thread_ = std::thread([this]() { runToneLoop(audio_source_, running_, 440.0, /*siren_mode=*/false); });

    return waitForSubscribedTrack(receiver_, sender_identity_, track_name, TrackKind::KIND_AUDIO, kSubscribeTimeout);
  }

  /// Stop feeding frames, drop the receiver's callbacks, and unpublish.
  void stop() {
    running_.store(false, std::memory_order_relaxed);
    if (video_thread_.joinable()) {
      video_thread_.join();
    }
    if (audio_thread_.joinable()) {
      audio_thread_.join();
    }
    if (video_track_ != nullptr) {
      receiver_.clearOnVideoFrameCallback(sender_identity_, video_track_->name());
      if (video_track_->publication()) {
        lockLocalParticipant(sender_)->unpublishTrack(video_track_->publication()->sid());
      }
      video_track_.reset();
    }
    if (audio_track_ != nullptr) {
      receiver_.clearOnAudioFrameCallback(sender_identity_, audio_track_->name());
      if (audio_track_->publication()) {
        lockLocalParticipant(sender_)->unpublishTrack(audio_track_->publication()->sid());
      }
      audio_track_.reset();
    }
  }

private:
  Room sender_;
  Room receiver_;
  std::string sender_identity_;
  bool connected_ = false;
  std::atomic<bool> running_{true};
  std::shared_ptr<VideoSource> video_source_;
  std::shared_ptr<LocalVideoTrack> video_track_;
  std::thread video_thread_;
  std::shared_ptr<AudioSource> audio_source_;
  std::shared_ptr<LocalAudioTrack> audio_track_;
  std::thread audio_thread_;
};

/// Registers the video callback from inside onTrackSubscribed -- the ordering
/// that always worked and must keep working.
class RegisterInDelegate : public RoomDelegate {
public:
  RegisterInDelegate(std::string track_name, std::atomic<int>& frames)
      : track_name_(std::move(track_name)), frames_(frames) {}

  void onTrackSubscribed(Room& room, const TrackSubscribedEvent& event) override {
    if (event.publication == nullptr || event.participant == nullptr || event.publication->name() != track_name_) {
      return;
    }
    room.setOnVideoFrameCallback(event.participant->identity(), track_name_,
                                 [this](const VideoFrame&, std::int64_t) { frames_.fetch_add(1); });
    registered_.store(true);
  }

  bool registered() const { return registered_.load(); }

private:
  std::string track_name_;
  std::atomic<int>& frames_;
  std::atomic<bool> registered_{false};
};

} // namespace

class FrameCallbackServerTest : public LiveKitTestBase {};

// The exact scenario from the issue: the receiver only registers once the
// subscription is complete, and does so from a thread that is not the one that
// delivered the event.
TEST_F(FrameCallbackServerTest, VideoCallbackRegisteredAfterSubscriptionReceivesFrames) {
  failIfNotConfigured();

  LateRegistrationFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  const std::string track_name = "late-video-callback";
  ASSERT_TRUE(fixture.publishVideoAndAwaitSubscription(track_name))
      << "Timed out waiting for the remote video subscription";
  ASSERT_TRUE(RoomTestAccess::hasRetainedSubscribedTrack(fixture.receiver(), fixture.senderIdentity(), track_name))
      << "The dispatcher must retain the subscription so a late registration can start a reader";
  ASSERT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 0u) << "No callback yet, so no reader";

  std::atomic<int> received_frames{0};
  registerFromAnotherThread([&]() {
    fixture.receiver().setOnVideoFrameCallback(fixture.senderIdentity(), track_name,
                                               [&](const VideoFrame&, std::int64_t) { received_frames.fetch_add(1); });
  });

  EXPECT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 1u)
      << "Late registration must start the reader immediately, not wait for another subscribe event";
  EXPECT_TRUE(waitFor([&]() { return received_frames.load() > 0; }, kFrameTimeout))
      << "No video frames arrived after late callback registration";
}

TEST_F(FrameCallbackServerTest, VideoFrameEventCallbackRegisteredAfterSubscriptionReceivesFrames) {
  failIfNotConfigured();

  LateRegistrationFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  const std::string track_name = "late-video-event-callback";
  ASSERT_TRUE(fixture.publishVideoAndAwaitSubscription(track_name));
  ASSERT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 0u);

  std::atomic<int> received_events{0};
  registerFromAnotherThread([&]() {
    fixture.receiver().setOnVideoFrameEventCallback(fixture.senderIdentity(), track_name,
                                                    [&](const VideoFrameEvent&) { received_events.fetch_add(1); });
  });

  EXPECT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 1u);
  EXPECT_TRUE(waitFor([&]() { return received_events.load() > 0; }, kFrameTimeout))
      << "No video frame events arrived after late callback registration";
}

TEST_F(FrameCallbackServerTest, AudioCallbackRegisteredAfterSubscriptionReceivesFrames) {
  failIfNotConfigured();

  LateRegistrationFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  const std::string track_name = "late-audio-callback";
  ASSERT_TRUE(fixture.publishAudioAndAwaitSubscription(track_name))
      << "Timed out waiting for the remote audio subscription";
  ASSERT_TRUE(RoomTestAccess::hasRetainedSubscribedTrack(fixture.receiver(), fixture.senderIdentity(), track_name));
  ASSERT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 0u);

  std::atomic<int> received_frames{0};
  registerFromAnotherThread([&]() {
    fixture.receiver().setOnAudioFrameCallback(fixture.senderIdentity(), track_name, [&](const AudioFrame& frame) {
      if (frame.totalSamples() > 0) {
        received_frames.fetch_add(1);
      }
    });
  });

  EXPECT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 1u);
  EXPECT_TRUE(waitFor([&]() { return received_frames.load() > 0; }, kFrameTimeout))
      << "No audio frames arrived after late callback registration";
}

// The subscription event is retained, not consumed: a late registration must
// also survive being cleared and registered again without another event.
TEST_F(FrameCallbackServerTest, ClearingAndReRegisteringAfterSubscriptionRestartsDelivery) {
  failIfNotConfigured();

  LateRegistrationFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  const std::string track_name = "late-video-reregister";
  ASSERT_TRUE(fixture.publishVideoAndAwaitSubscription(track_name));

  std::atomic<int> first_frames{0};
  fixture.receiver().setOnVideoFrameCallback(fixture.senderIdentity(), track_name,
                                             [&](const VideoFrame&, std::int64_t) { first_frames.fetch_add(1); });
  ASSERT_TRUE(waitFor([&]() { return first_frames.load() > 0; }, kFrameTimeout));

  fixture.receiver().clearOnVideoFrameCallback(fixture.senderIdentity(), track_name);
  EXPECT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 0u);
  EXPECT_TRUE(RoomTestAccess::hasRetainedSubscribedTrack(fixture.receiver(), fixture.senderIdentity(), track_name))
      << "Clearing the callback must not forget the subscription";

  std::atomic<int> second_frames{0};
  registerFromAnotherThread([&]() {
    fixture.receiver().setOnVideoFrameCallback(fixture.senderIdentity(), track_name,
                                               [&](const VideoFrame&, std::int64_t) { second_frames.fetch_add(1); });
  });
  EXPECT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 1u);
  EXPECT_TRUE(waitFor([&]() { return second_frames.load() > 0; }, kFrameTimeout))
      << "Re-registering after a clear never received a frame";
}

// The ordering that has always worked -- registering from inside
// onTrackSubscribed -- must keep working alongside the late path. Room calls
// the dispatcher after the delegate returns, so the reader must start exactly
// once rather than being duplicated by the registration's own start.
TEST_F(FrameCallbackServerTest, VideoCallbackRegisteredInsideOnTrackSubscribedReceivesFrames) {
  failIfNotConfigured();

  LateRegistrationFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  const std::string track_name = "in-delegate-video-callback";
  std::atomic<int> received_frames{0};
  RegisterInDelegate delegate(track_name, received_frames);
  fixture.receiver().setDelegate(&delegate);

  ASSERT_TRUE(fixture.publishVideoAndAwaitSubscription(track_name));
  ASSERT_TRUE(waitFor([&]() { return delegate.registered(); }, kSubscribeTimeout))
      << "onTrackSubscribed never fired for the published track";

  EXPECT_TRUE(waitFor([&]() { return received_frames.load() > 0; }, kFrameTimeout))
      << "No video frames arrived for a callback registered inside onTrackSubscribed";
  EXPECT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 1u)
      << "Registering inside the delegate must not start a second reader for the same subscription";

  fixture.stop();
  fixture.receiver().setDelegate(nullptr);
}

} // namespace livekit::test
