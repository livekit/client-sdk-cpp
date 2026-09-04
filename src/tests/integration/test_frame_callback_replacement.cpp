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

/// @file test_frame_callback_replacement.cpp
/// @brief End-to-end coverage for in-place frame callback replacement.
///
/// Registering a frame callback again for the same (participant, track name)
/// replaces it in place: the previous reader is stopped and joined, then a fresh
/// reader is started bound to the new callback. Reader threads hold their own
/// copy of the callback, so without that teardown the old callback keeps
/// receiving frames -- a silent no-op these tests are designed to catch.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "tests/common/audio_utils.h"
#include "tests/common/room_test_access.h"
#include "tests/common/test_common.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr auto kSubscribeTimeout = 15s;
constexpr auto kFrameTimeout = 15s;
/// How long to watch a retired callback before concluding it has gone quiet.
constexpr auto kQuiescenceGracePeriod = 1s;
/// Upper bound on any call that must not deadlock. Generous enough to absorb a
/// slow join, tight enough that a real deadlock fails instead of hanging CI.
constexpr auto kNoDeadlockTimeout = 30s;

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
/// published by @p identity.
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

/// Drives a VideoSource on a background thread for the life of the object.
class VideoPublisher {
public:
  explicit VideoPublisher(std::shared_ptr<VideoSource> source) : source_(std::move(source)) {
    thread_ = std::thread([this]() {
      VideoFrame frame = VideoFrame::create(kFrameWidth, kFrameHeight, VideoBufferType::RGBA);
      std::fill(frame.data(), frame.data() + frame.dataSize(), 0x7f);
      while (running_.load(std::memory_order_relaxed)) {
        try {
          source_->captureFrame(frame);
        } catch (...) {
          break;
        }
        std::this_thread::sleep_for(20ms);
      }
    });
  }

  VideoPublisher(const VideoPublisher&) = delete;
  VideoPublisher& operator=(const VideoPublisher&) = delete;

  void stop() {
    running_.store(false, std::memory_order_relaxed);
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  ~VideoPublisher() { stop(); }

private:
  std::shared_ptr<VideoSource> source_;
  std::atomic<bool> running_{true};
  std::thread thread_;
};

/// Run @p action on a worker thread and fail if it does not return in time.
/// Used for calls that join a reader thread, where a regression shows up as a
/// hang rather than a wrong value.
[[nodiscard]] bool completesWithoutDeadlock(const std::function<void()>& action,
                                            std::chrono::milliseconds timeout = kNoDeadlockTimeout) {
  auto future = std::async(std::launch::async, action);
  return future.wait_for(timeout) == std::future_status::ready;
}

/// Two connected rooms plus one published video track, with the receiver
/// confirmed subscribed. Every video test starts from this state.
class VideoFixture {
public:
  VideoFixture(const std::string& url, const std::string& token_a, const std::string& token_b,
               std::string track_name = "replacement-track")
      : track_name_(std::move(track_name)) {
    const RoomOptions options;
    connected_ = receiver_.connect(url, token_b, options) && sender_.connect(url, token_a, options);
    if (!connected_) {
      return;
    }
    if (sender_.localParticipant().expired() || receiver_.localParticipant().expired()) {
      connected_ = false;
      return;
    }
    sender_identity_ = lockLocalParticipant(sender_)->identity();
  }

  /// Publish the video track and wait for the receiver to subscribe.
  bool publishAndAwaitSubscription() {
    if (!connected_ || !waitForParticipant(&receiver_, sender_identity_, kSubscribeTimeout)) {
      return false;
    }
    source_ = std::make_shared<VideoSource>(kFrameWidth, kFrameHeight);
    track_ = LocalVideoTrack::createLocalVideoTrack(track_name_, source_);

    TrackPublishOptions publish_options;
    publish_options.source = TrackSource::SOURCE_CAMERA;
    publish_options.simulcast = false;
    lockLocalParticipant(sender_)->publishTrack(track_, publish_options);

    publisher_ = std::make_unique<VideoPublisher>(source_);
    return waitForSubscribedTrack(receiver_, sender_identity_, track_name_, TrackKind::KIND_VIDEO, kSubscribeTimeout);
  }

  void unpublish() {
    if (track_ && track_->publication()) {
      lockLocalParticipant(sender_)->unpublishTrack(track_->publication()->sid());
    }
  }

  void teardown() {
    if (publisher_) {
      publisher_->stop();
    }
    receiver_.clearOnVideoFrameCallback(sender_identity_, track_name_);
    unpublish();
  }

  ~VideoFixture() {
    if (publisher_) {
      publisher_->stop();
    }
  }

  bool connected() const { return connected_; }
  Room& receiver() { return receiver_; }
  Room& sender() { return sender_; }
  const std::string& senderIdentity() const { return sender_identity_; }
  const std::string& trackName() const { return track_name_; }
  const std::shared_ptr<VideoSource>& source() const { return source_; }

private:
  Room sender_;
  Room receiver_;
  std::string track_name_;
  std::string sender_identity_;
  bool connected_ = false;
  std::shared_ptr<VideoSource> source_;
  std::shared_ptr<LocalVideoTrack> track_;
  std::unique_ptr<VideoPublisher> publisher_;
};

/// Assert that @p counter stops advancing, i.e. its callback has been retired.
[[nodiscard]] bool wentQuiet(const std::atomic<int>& counter) {
  const int before = counter.load();
  std::this_thread::sleep_for(kQuiescenceGracePeriod);
  return counter.load() == before;
}

} // namespace

class FrameCallbackReplacementTest : public LiveKitTestBase {};

// ============================================================================
// Core replacement
// ============================================================================

// The canonical regression: before the in-place replacement fix, the reader
// thread kept invoking its own copy of callback A forever and B never fired.
TEST_F(FrameCallbackReplacementTest, ReplacingActiveVideoCallbackSwitchesFrameDelivery) {
  failIfNotConfigured();

  VideoFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  std::atomic<int> a_frames{0};
  std::atomic<int> b_frames{0};

  fixture.receiver().setOnVideoFrameCallback(fixture.senderIdentity(), fixture.trackName(),
                                             [&a_frames](const VideoFrame&, std::int64_t) { a_frames.fetch_add(1); });

  ASSERT_TRUE(fixture.publishAndAwaitSubscription());
  ASSERT_TRUE(waitFor([&]() { return a_frames.load() > 0; }, kFrameTimeout)) << "First callback never received a frame";

  fixture.receiver().setOnVideoFrameCallback(fixture.senderIdentity(), fixture.trackName(),
                                             [&b_frames](const VideoFrame&, std::int64_t) { b_frames.fetch_add(1); });

  EXPECT_TRUE(waitFor([&]() { return b_frames.load() > 0; }, kFrameTimeout))
      << "Replacement callback never received a frame";
  EXPECT_TRUE(wentQuiet(a_frames)) << "Replaced callback is still receiving frames; its reader was not stopped";
  EXPECT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 1u);

  fixture.teardown();
}

TEST_F(FrameCallbackReplacementTest, ReplacingActiveAudioCallbackSwitchesFrameDelivery) {
  failIfNotConfigured();

  Room sender_room;
  Room receiver_room;
  const RoomOptions options;
  ASSERT_TRUE(receiver_room.connect(config_.url, config_.token_b, options));
  ASSERT_TRUE(sender_room.connect(config_.url, config_.token_a, options));

  const std::string sender_identity = lockLocalParticipant(sender_room)->identity();
  ASSERT_TRUE(waitForParticipant(&receiver_room, sender_identity, kSubscribeTimeout));

  const std::string track_name = "replacement-audio";
  std::atomic<int> a_frames{0};
  std::atomic<int> b_frames{0};

  receiver_room.setOnAudioFrameCallback(sender_identity, track_name, [&a_frames](const AudioFrame& frame) {
    if (frame.totalSamples() > 0) {
      a_frames.fetch_add(1);
    }
  });

  auto source = std::make_shared<AudioSource>(kDefaultAudioSampleRate, kDefaultAudioChannels);
  auto track = LocalAudioTrack::createLocalAudioTrack(track_name, source);
  TrackPublishOptions publish_options;
  publish_options.source = TrackSource::SOURCE_MICROPHONE;
  lockLocalParticipant(sender_room)->publishTrack(track, publish_options);

  std::atomic<bool> publishing{true};
  std::thread publisher([&]() { runToneLoop(source, publishing, 440.0, /*siren_mode=*/false); });

  ASSERT_TRUE(
      waitForSubscribedTrack(receiver_room, sender_identity, track_name, TrackKind::KIND_AUDIO, kSubscribeTimeout));
  ASSERT_TRUE(waitFor([&]() { return a_frames.load() > 0; }, kFrameTimeout)) << "First callback never received a frame";

  receiver_room.setOnAudioFrameCallback(sender_identity, track_name, [&b_frames](const AudioFrame& frame) {
    if (frame.totalSamples() > 0) {
      b_frames.fetch_add(1);
    }
  });

  EXPECT_TRUE(waitFor([&]() { return b_frames.load() > 0; }, kFrameTimeout))
      << "Replacement callback never received a frame";
  EXPECT_TRUE(wentQuiet(a_frames)) << "Replaced callback is still receiving frames";
  EXPECT_EQ(RoomTestAccess::activeReaderCount(receiver_room), 1u);

  publishing.store(false);
  publisher.join();
  receiver_room.clearOnAudioFrameCallback(sender_identity, track_name);
  if (track->publication()) {
    lockLocalParticipant(sender_room)->unpublishTrack(track->publication()->sid());
  }
}

// The legacy and event video callbacks share one registration slot, so
// registering either must displace the other and stop its reader.
TEST_F(FrameCallbackReplacementTest, ReplacingVideoCallbackWithEventCallbackSwitchesDelivery) {
  failIfNotConfigured();

  VideoFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  std::atomic<int> legacy_frames{0};
  std::atomic<int> event_frames{0};

  fixture.receiver().setOnVideoFrameCallback(
      fixture.senderIdentity(), fixture.trackName(),
      [&legacy_frames](const VideoFrame&, std::int64_t) { legacy_frames.fetch_add(1); });

  ASSERT_TRUE(fixture.publishAndAwaitSubscription());
  ASSERT_TRUE(waitFor([&]() { return legacy_frames.load() > 0; }, kFrameTimeout));

  fixture.receiver().setOnVideoFrameEventCallback(
      fixture.senderIdentity(), fixture.trackName(),
      [&event_frames](const VideoFrameEvent&) { event_frames.fetch_add(1); });

  EXPECT_TRUE(waitFor([&]() { return event_frames.load() > 0; }, kFrameTimeout))
      << "Event callback never received a frame after displacing the legacy callback";
  EXPECT_TRUE(wentQuiet(legacy_frames)) << "Displaced legacy callback is still receiving frames";
  EXPECT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 1u);

  fixture.teardown();
}

// ============================================================================
// Long-running callbacks
// ============================================================================

// Replacement joins the previous reader, so it must wait out an in-flight
// callback invocation rather than abandoning it mid-frame. This is the guarantee
// the API documents: when the setter returns, the old callback has finished.
TEST_F(FrameCallbackReplacementTest, SetOnVideoFrameCallbackBlocksUntilSlowCallbackReturns) {
  failIfNotConfigured();

  VideoFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  constexpr auto kSlowCallbackDuration = 2s;
  std::atomic<bool> slow_entered{false};
  std::atomic<bool> slow_exited{false};
  std::atomic<int> fast_frames{0};

  fixture.receiver().setOnVideoFrameCallback(fixture.senderIdentity(), fixture.trackName(),
                                             [&](const VideoFrame&, std::int64_t) {
                                               slow_entered.store(true);
                                               std::this_thread::sleep_for(kSlowCallbackDuration);
                                               slow_exited.store(true);
                                             });

  ASSERT_TRUE(fixture.publishAndAwaitSubscription());
  ASSERT_TRUE(waitFor([&]() { return slow_entered.load(); }, kFrameTimeout))
      << "Slow callback never started an invocation";
  ASSERT_FALSE(slow_exited.load()) << "Slow callback finished before the replacement was attempted";

  const auto started_at = std::chrono::steady_clock::now();
  bool exited_before_return = false;
  const bool completed = completesWithoutDeadlock([&]() {
    fixture.receiver().setOnVideoFrameCallback(
        fixture.senderIdentity(), fixture.trackName(),
        [&fast_frames](const VideoFrame&, std::int64_t) { fast_frames.fetch_add(1); });
    exited_before_return = slow_exited.load();
  });
  const auto elapsed = std::chrono::steady_clock::now() - started_at;

  ASSERT_TRUE(completed) << "setOnVideoFrameCallback did not return; the join deadlocked";
  EXPECT_TRUE(exited_before_return) << "Setter returned while the previous callback was still executing";
  EXPECT_GE(elapsed, 500ms) << "Setter returned too quickly to have waited on the in-flight callback";

  EXPECT_TRUE(waitFor([&]() { return fast_frames.load() > 0; }, kFrameTimeout));
  EXPECT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 1u);

  fixture.teardown();
}

// The join happens outside the dispatcher lock, so a slow callback on one
// subscription must not stall readers for other subscriptions.
TEST_F(FrameCallbackReplacementTest, SlowCallbackDoesNotStallOtherSubscriptionReaders) {
  failIfNotConfigured();

  Room sender_room;
  Room receiver_room;
  const RoomOptions options;
  ASSERT_TRUE(receiver_room.connect(config_.url, config_.token_b, options));
  ASSERT_TRUE(sender_room.connect(config_.url, config_.token_a, options));

  const std::string sender_identity = lockLocalParticipant(sender_room)->identity();
  ASSERT_TRUE(waitForParticipant(&receiver_room, sender_identity, kSubscribeTimeout));

  const std::string slow_track_name = "slow-track";
  const std::string fast_track_name = "fast-track";

  std::atomic<int> slow_invocations{0};
  std::atomic<int> fast_frames{0};

  receiver_room.setOnVideoFrameCallback(sender_identity, slow_track_name, [&](const VideoFrame&, std::int64_t) {
    slow_invocations.fetch_add(1);
    std::this_thread::sleep_for(2s);
  });
  receiver_room.setOnVideoFrameCallback(sender_identity, fast_track_name,
                                        [&fast_frames](const VideoFrame&, std::int64_t) { fast_frames.fetch_add(1); });

  auto slow_source = std::make_shared<VideoSource>(kFrameWidth, kFrameHeight);
  auto fast_source = std::make_shared<VideoSource>(kFrameWidth, kFrameHeight);
  auto slow_track = LocalVideoTrack::createLocalVideoTrack(slow_track_name, slow_source);
  auto fast_track = LocalVideoTrack::createLocalVideoTrack(fast_track_name, fast_source);

  TrackPublishOptions publish_options;
  publish_options.source = TrackSource::SOURCE_CAMERA;
  publish_options.simulcast = false;
  lockLocalParticipant(sender_room)->publishTrack(slow_track, publish_options);
  lockLocalParticipant(sender_room)->publishTrack(fast_track, publish_options);

  VideoPublisher slow_publisher(slow_source);
  VideoPublisher fast_publisher(fast_source);

  ASSERT_TRUE(waitForSubscribedTrack(receiver_room, sender_identity, slow_track_name, TrackKind::KIND_VIDEO,
                                     kSubscribeTimeout));
  ASSERT_TRUE(waitForSubscribedTrack(receiver_room, sender_identity, fast_track_name, TrackKind::KIND_VIDEO,
                                     kSubscribeTimeout));
  ASSERT_TRUE(waitFor([&]() { return slow_invocations.load() > 0; }, kFrameTimeout));

  const int fast_before = fast_frames.load();
  const bool completed = completesWithoutDeadlock([&]() {
    receiver_room.setOnVideoFrameCallback(sender_identity, slow_track_name, [](const VideoFrame&, std::int64_t) {});
  });
  ASSERT_TRUE(completed) << "Replacing the slow callback deadlocked";

  EXPECT_GT(fast_frames.load(), fast_before)
      << "The unrelated fast reader stalled while the slow callback was being replaced";
  EXPECT_EQ(RoomTestAccess::activeReaderCount(receiver_room), 2u);

  slow_publisher.stop();
  fast_publisher.stop();
  receiver_room.clearOnVideoFrameCallback(sender_identity, slow_track_name);
  receiver_room.clearOnVideoFrameCallback(sender_identity, fast_track_name);
  if (slow_track->publication()) {
    lockLocalParticipant(sender_room)->unpublishTrack(slow_track->publication()->sid());
  }
  if (fast_track->publication()) {
    lockLocalParticipant(sender_room)->unpublishTrack(fast_track->publication()->sid());
  }
}

// ============================================================================
// Multiple and concurrent calls
// ============================================================================

// Each replacement must stop exactly one reader and start exactly one. A leak
// shows up as a growing reader count or as several generations firing at once.
TEST_F(FrameCallbackReplacementTest, RepeatedReplacementUnderLoadLeavesExactlyOneActiveReader) {
  failIfNotConfigured();

  constexpr int kGenerations = 20;

  VideoFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  std::vector<std::unique_ptr<std::atomic<int>>> counters;
  counters.reserve(kGenerations);
  for (int i = 0; i < kGenerations; ++i) {
    counters.push_back(std::make_unique<std::atomic<int>>(0));
  }

  auto* first = counters.front().get();
  fixture.receiver().setOnVideoFrameCallback(fixture.senderIdentity(), fixture.trackName(),
                                             [first](const VideoFrame&, std::int64_t) { first->fetch_add(1); });

  ASSERT_TRUE(fixture.publishAndAwaitSubscription());
  ASSERT_TRUE(waitFor([&]() { return first->load() > 0; }, kFrameTimeout));

  for (int i = 1; i < kGenerations; ++i) {
    auto* counter = counters[static_cast<std::size_t>(i)].get();
    const bool completed = completesWithoutDeadlock([&]() {
      fixture.receiver().setOnVideoFrameCallback(fixture.senderIdentity(), fixture.trackName(),
                                                 [counter](const VideoFrame&, std::int64_t) { counter->fetch_add(1); });
    });
    ASSERT_TRUE(completed) << "Replacement " << i << " deadlocked";
    ASSERT_LE(RoomTestAccess::activeReaderCount(fixture.receiver()), 1u)
        << "Reader count grew during replacement " << i;
    std::this_thread::sleep_for(50ms);
  }

  auto* last = counters.back().get();
  EXPECT_TRUE(waitFor([&]() { return last->load() > 0; }, kFrameTimeout))
      << "The final callback generation never received a frame";
  EXPECT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 1u);

  // Every earlier generation must be retired: snapshot all of them, wait, and
  // confirm none advanced.
  std::vector<int> before;
  before.reserve(counters.size());
  for (const auto& counter : counters) {
    before.push_back(counter->load());
  }
  std::this_thread::sleep_for(kQuiescenceGracePeriod);
  for (std::size_t i = 0; i + 1 < counters.size(); ++i) {
    EXPECT_EQ(counters[i]->load(), before[i]) << "Retired callback generation " << i << " is still receiving frames";
  }

  fixture.teardown();
}

// Concurrent replacements must serialize on the dispatcher lock without
// deadlocking, double-starting readers, or losing the final registration.
TEST_F(FrameCallbackReplacementTest, ConcurrentReplacementFromMultipleThreadsIsSerialized) {
  failIfNotConfigured();

  constexpr int kThreads = 4;
  constexpr auto kChurnDuration = 2s;

  VideoFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  std::atomic<int> initial_frames{0};
  fixture.receiver().setOnVideoFrameCallback(
      fixture.senderIdentity(), fixture.trackName(),
      [&initial_frames](const VideoFrame&, std::int64_t) { initial_frames.fetch_add(1); });

  ASSERT_TRUE(fixture.publishAndAwaitSubscription());
  ASSERT_TRUE(waitFor([&]() { return initial_frames.load() > 0; }, kFrameTimeout));

  std::atomic<bool> churning{true};
  std::atomic<int> max_readers_seen{0};
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    workers.emplace_back([&]() {
      while (churning.load(std::memory_order_relaxed)) {
        fixture.receiver().setOnVideoFrameCallback(fixture.senderIdentity(), fixture.trackName(),
                                                   [](const VideoFrame&, std::int64_t) {});
        const auto readers = static_cast<int>(RoomTestAccess::activeReaderCount(fixture.receiver()));
        int previous = max_readers_seen.load();
        while (readers > previous && !max_readers_seen.compare_exchange_weak(previous, readers)) {
        }
        std::this_thread::sleep_for(10ms);
      }
    });
  }

  auto churn_done = std::async(std::launch::async, [&]() {
    std::this_thread::sleep_for(kChurnDuration);
    churning.store(false, std::memory_order_relaxed);
    for (auto& worker : workers) {
      worker.join();
    }
  });
  ASSERT_EQ(churn_done.wait_for(kNoDeadlockTimeout), std::future_status::ready) << "Concurrent replacement deadlocked";

  EXPECT_LE(max_readers_seen.load(), 1) << "Concurrent replacements started more than one reader for the same key";

  // The registration surviving the churn must still deliver frames.
  std::atomic<int> final_frames{0};
  fixture.receiver().setOnVideoFrameCallback(
      fixture.senderIdentity(), fixture.trackName(),
      [&final_frames](const VideoFrame&, std::int64_t) { final_frames.fetch_add(1); });
  EXPECT_TRUE(waitFor([&]() { return final_frames.load() > 0; }, kFrameTimeout))
      << "Frame delivery did not recover after concurrent replacement";
  EXPECT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 1u);

  fixture.teardown();
}

// Deferred start: with no subscription yet there is no reader to stop, and the
// newest registration is the one the reader must bind when the track arrives.
TEST_F(FrameCallbackReplacementTest, ReplacingCallbackBeforeSubscriptionUsesNewestCallback) {
  failIfNotConfigured();

  VideoFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  std::atomic<int> a_frames{0};
  std::atomic<int> b_frames{0};

  fixture.receiver().setOnVideoFrameCallback(fixture.senderIdentity(), fixture.trackName(),
                                             [&a_frames](const VideoFrame&, std::int64_t) { a_frames.fetch_add(1); });
  fixture.receiver().setOnVideoFrameCallback(fixture.senderIdentity(), fixture.trackName(),
                                             [&b_frames](const VideoFrame&, std::int64_t) { b_frames.fetch_add(1); });
  ASSERT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 0u) << "No reader should exist before subscription";

  ASSERT_TRUE(fixture.publishAndAwaitSubscription());

  EXPECT_TRUE(waitFor([&]() { return b_frames.load() > 0; }, kFrameTimeout))
      << "The newest pre-subscription callback never received a frame";
  EXPECT_EQ(a_frames.load(), 0) << "The overwritten pre-subscription callback must never fire";
  EXPECT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 1u);

  fixture.teardown();
}

// Replacement while unsubscribed must survive the resubscribe: the registration
// persists across unpublish, and the new callback binds on republish.
TEST_F(FrameCallbackReplacementTest, ReplacementSurvivesUnpublishAndRepublish) {
  failIfNotConfigured();

  VideoFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  std::atomic<int> a_frames{0};
  std::atomic<int> b_frames{0};

  fixture.receiver().setOnVideoFrameCallback(fixture.senderIdentity(), fixture.trackName(),
                                             [&a_frames](const VideoFrame&, std::int64_t) { a_frames.fetch_add(1); });

  ASSERT_TRUE(fixture.publishAndAwaitSubscription());
  ASSERT_TRUE(waitFor([&]() { return a_frames.load() > 0; }, kFrameTimeout));

  fixture.unpublish();
  ASSERT_TRUE(waitFor([&]() { return RoomTestAccess::activeReaderCount(fixture.receiver()) == 0u; }, kSubscribeTimeout))
      << "Reader was not torn down on unpublish";

  fixture.receiver().setOnVideoFrameCallback(fixture.senderIdentity(), fixture.trackName(),
                                             [&b_frames](const VideoFrame&, std::int64_t) { b_frames.fetch_add(1); });

  ASSERT_TRUE(fixture.publishAndAwaitSubscription()) << "Republished track was never subscribed";
  EXPECT_TRUE(waitFor([&]() { return b_frames.load() > 0; }, kFrameTimeout))
      << "Replacement callback never received a frame from the new publication";
  EXPECT_TRUE(wentQuiet(a_frames)) << "Replaced callback is still receiving frames after republish";
  EXPECT_EQ(RoomTestAccess::activeReaderCount(fixture.receiver()), 1u);

  fixture.teardown();
}

// ============================================================================
// Re-entrancy
// ============================================================================

// Registering from inside the frame callback would make the join a self-join.
// Media readers are detached instead, so the call must return rather than hang.
TEST_F(FrameCallbackReplacementTest, SetOnVideoFrameCallbackFromInsideCallbackDoesNotDeadlock) {
  failIfNotConfigured();

  VideoFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  std::atomic<bool> reentrant_call_returned{false};
  std::atomic<bool> attempted{false};
  std::atomic<int> replacement_frames{0};

  fixture.receiver().setOnVideoFrameCallback(
      fixture.senderIdentity(), fixture.trackName(), [&](const VideoFrame&, std::int64_t) {
        if (attempted.exchange(true)) {
          return;
        }
        fixture.receiver().setOnVideoFrameCallback(
            fixture.senderIdentity(), fixture.trackName(),
            [&replacement_frames](const VideoFrame&, std::int64_t) { replacement_frames.fetch_add(1); });
        reentrant_call_returned.store(true);
      });

  ASSERT_TRUE(fixture.publishAndAwaitSubscription());
  EXPECT_TRUE(waitFor([&]() { return reentrant_call_returned.load(); }, kNoDeadlockTimeout))
      << "Re-entrant setOnVideoFrameCallback never returned; the reader self-joined";

  // The detached reader exits on its own, and teardown must still complete.
  const bool torn_down = completesWithoutDeadlock([&]() { fixture.teardown(); });
  EXPECT_TRUE(torn_down) << "Teardown deadlocked after a re-entrant registration";
}

TEST_F(FrameCallbackReplacementTest, ClearOnVideoFrameCallbackFromInsideCallbackDoesNotDeadlock) {
  failIfNotConfigured();

  VideoFixture fixture(config_.url, config_.token_a, config_.token_b);
  ASSERT_TRUE(fixture.connected());

  std::atomic<bool> reentrant_call_returned{false};
  std::atomic<bool> attempted{false};

  fixture.receiver().setOnVideoFrameCallback(
      fixture.senderIdentity(), fixture.trackName(), [&](const VideoFrame&, std::int64_t) {
        if (attempted.exchange(true)) {
          return;
        }
        fixture.receiver().clearOnVideoFrameCallback(fixture.senderIdentity(), fixture.trackName());
        reentrant_call_returned.store(true);
      });

  ASSERT_TRUE(fixture.publishAndAwaitSubscription());
  EXPECT_TRUE(waitFor([&]() { return reentrant_call_returned.load(); }, kNoDeadlockTimeout))
      << "Re-entrant clearOnVideoFrameCallback never returned; the reader self-joined";

  const bool torn_down = completesWithoutDeadlock([&]() { fixture.teardown(); });
  EXPECT_TRUE(torn_down) << "Teardown deadlocked after a re-entrant clear";
}

// Data readers re-enter the dispatcher after their callback returns, so they
// cannot be detached. The re-entrant removal is refused and the reader is left
// for teardown to reap -- which must still join cleanly.
TEST_F(FrameCallbackReplacementTest, RemoveDataCallbackFromInsideDataCallbackIsRefusedWithoutDeadlock) {
  failIfNotConfigured();

  Room sender_room;
  Room receiver_room;
  const RoomOptions options;
  ASSERT_TRUE(receiver_room.connect(config_.url, config_.token_b, options));
  ASSERT_TRUE(sender_room.connect(config_.url, config_.token_a, options));

  const std::string sender_identity = lockLocalParticipant(sender_room)->identity();
  ASSERT_TRUE(waitForParticipant(&receiver_room, sender_identity, kSubscribeTimeout));

  const std::string track_name = "reentrant-data";
  std::atomic<bool> reentrant_call_returned{false};
  std::atomic<bool> attempted{false};
  DataFrameCallbackId callback_id = 0;

  callback_id = receiver_room.addOnDataFrameCallback(
      sender_identity, track_name, [&](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {
        if (attempted.exchange(true)) {
          return;
        }
        receiver_room.removeOnDataFrameCallback(callback_id);
        reentrant_call_returned.store(true);
      });

  auto publish_result = lockLocalParticipant(sender_room)->publishDataTrack(track_name);
  ASSERT_TRUE(publish_result) << "Failed to publish data track";
  auto local_track = publish_result.value();

  std::atomic<bool> pushing{true};
  std::thread pusher([&]() {
    DataTrackFrame frame;
    frame.payload.assign(32, 0x5A);
    while (pushing.load(std::memory_order_relaxed)) {
      (void)local_track->tryPush(frame);
      std::this_thread::sleep_for(50ms);
    }
  });

  EXPECT_TRUE(waitFor([&]() { return reentrant_call_returned.load(); }, kNoDeadlockTimeout))
      << "Re-entrant removeOnDataFrameCallback never returned; the data reader self-joined";

  pushing.store(false, std::memory_order_relaxed);
  pusher.join();

  // The refused removal left the reader in place; disconnect must still reap it.
  const bool disconnected = completesWithoutDeadlock([&]() {
    local_track->unpublishDataTrack();
    receiver_room.disconnect();
  });
  EXPECT_TRUE(disconnected) << "Disconnect deadlocked while reaping the refused data reader";
}

} // namespace livekit::test
