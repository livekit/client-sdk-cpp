/*
 * Copyright 2025 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/// @file test_subscription_thread_dispatcher.cpp
/// @brief Unit tests for SubscriptionThreadDispatcher registration state.

#include <gtest/gtest.h>
#include <livekit/livekit.h>
#include <livekit/remote_data_track.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../common/remote_data_track_test_access.h"
#include "livekit/subscription_thread_dispatcher.h"

// This file unit-tests SubscriptionThreadDispatcher's internals directly, an
// intentional in-tree use of an API that is deprecated for external
// consumers; suppress the deprecation warning throughout.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

namespace livekit {

namespace {

using namespace std::chrono_literals;

/// Minimal Track used to drive audio/video reader startup decisions without a
/// live FFI handle. The SID-skip check runs before any FFI call, so an invalid
/// handle is sufficient to exercise it deterministically.
class FakeMediaTrack : public Track {
public:
  FakeMediaTrack(std::string sid, TrackKind kind)
      : Track(FfiHandle(0), std::move(sid), "track", kind, StreamState::STATE_ACTIVE, false, true) {}
};

/// Minimal frames used to invoke a stored callback directly, so tests can prove
/// which callback a registration slot actually holds.
AudioFrame makeAudioFrame() { return AudioFrame::create(48000, 1, 480); }
VideoFrame makeVideoFrame() { return VideoFrame::create(16, 16, VideoBufferType::RGBA); }

template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds timeout) {
  const auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < timeout) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(5ms);
  }
  return predicate();
}

} // namespace

class SubscriptionThreadDispatcherTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogLevel::Info); }

  void TearDown() override { livekit::shutdown(); }

  using CallbackKey = SubscriptionThreadDispatcher::CallbackKey;
  using CallbackKeyHash = SubscriptionThreadDispatcher::CallbackKeyHash;
  using DataCallbackKey = SubscriptionThreadDispatcher::DataCallbackKey;
  using DataCallbackKeyHash = SubscriptionThreadDispatcher::DataCallbackKeyHash;

  using ActiveDataReader = SubscriptionThreadDispatcher::ActiveDataReader;

  static auto& audioCallbacks(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.audio_callbacks_; }
  static auto& videoCallbacks(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.video_callbacks_; }
  static auto& activeReaders(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.active_readers_; }
  static auto& subscribedTracks(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.subscribed_tracks_; }
  static auto& dataCallbacks(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.data_callbacks_; }
  static auto& activeDataReaders(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.active_data_readers_; }
  static auto& remoteDataTracks(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.remote_data_tracks_; }
  static auto& drainingReaders(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.draining_readers_; }
  static int maxActiveReaders() { return SubscriptionThreadDispatcher::kMaxActiveReaders; }
  static bool isSelfThread(std::thread::id id) { return SubscriptionThreadDispatcher::isSelfThread(id); }
  static std::size_t activeReaderCount(SubscriptionThreadDispatcher& dispatcher) {
    const std::scoped_lock<std::mutex> lock(dispatcher.lock_);
    return dispatcher.active_readers_.size();
  }
  static std::size_t activeDataReaderCount(SubscriptionThreadDispatcher& dispatcher) {
    const std::scoped_lock<std::mutex> lock(dispatcher.lock_);
    return dispatcher.active_data_readers_.size();
  }

  static std::thread extractDataReader(SubscriptionThreadDispatcher& dispatcher, DataFrameCallbackId id) {
    const std::scoped_lock<std::mutex> lock(dispatcher.lock_);
    return dispatcher.extractDataReaderThreadLocked(id);
  }

  static void markDataReaderFinished(const std::shared_ptr<ActiveDataReader>& reader) {
    SubscriptionThreadDispatcher::markDataReaderFinished(reader);
  }

  static void disposeReaderThread(SubscriptionThreadDispatcher& dispatcher, std::thread&& thread) {
    dispatcher.disposeReaderThread(std::move(thread), "SubscriptionThreadDispatcherTest");
  }

  static std::thread startDataReader(SubscriptionThreadDispatcher& dispatcher, DataFrameCallbackId id,
                                     const DataCallbackKey& key, const std::shared_ptr<RemoteDataTrack>& track) {
    const std::scoped_lock<std::mutex> lock(dispatcher.lock_);
    return dispatcher.startDataReaderLocked(id, key, track,
                                            [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
  }

  /// Seed an active media reader for @p key whose thread does real (trivial)
  /// work and records that it ran. Joining it is the only way the recorded
  /// flag is guaranteed visible, so a passing assertion on @p ran after a
  /// lifecycle call proves that call joined the previous reader.
  static void seedJoinableReader(SubscriptionThreadDispatcher& dispatcher, const CallbackKey& key,
                                 std::atomic<bool>& ran, const std::string& sid = "TR_seeded") {
    auto& reader = activeReaders(dispatcher)[key];
    reader.track_sid = sid;
    reader.thread = std::thread([&ran]() { ran.store(true); });
    reader.thread_id = reader.thread.get_id();
  }

  /// Seed an active media reader for @p key whose thread runs @p body, so a
  /// test can drive a lifecycle call from *inside* the reader's own thread --
  /// the re-entrant case where joining would be a self-join. @p ready gates the
  /// body until the std::thread has been fully assigned into the slot.
  static void seedSelfCallingReader(SubscriptionThreadDispatcher& dispatcher, const CallbackKey& key,
                                    std::shared_future<void> ready, std::function<void()> body) {
    auto& reader = activeReaders(dispatcher)[key];
    reader.track_sid = "TR_self";
    reader.thread = std::thread([ready = std::move(ready), body = std::move(body)]() {
      ready.wait();
      body();
    });
    reader.thread_id = reader.thread.get_id();
  }
};

// ============================================================================
// CallbackKey equality
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyEqualKeysCompareEqual) {
  CallbackKey a{"alice", "mic-main"};
  CallbackKey b{"alice", "mic-main"};
  EXPECT_TRUE(a == b);
}

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyDifferentIdentityNotEqual) {
  CallbackKey a{"alice", "mic-main"};
  CallbackKey b{"bob", "mic-main"};
  EXPECT_FALSE(a == b);
}

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyDifferentTrackNameNotEqual) {
  CallbackKey a{"alice", "cam-main"};
  CallbackKey b{"alice", "cam-backup"};
  EXPECT_FALSE(a == b);
}

// ============================================================================
// CallbackKeyHash
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyHashEqualKeysProduceSameHash) {
  CallbackKey a{"alice", "mic-main"};
  CallbackKey b{"alice", "mic-main"};
  CallbackKeyHash hasher;
  EXPECT_EQ(hasher(a), hasher(b));
}

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyHashDifferentKeysLikelyDifferentHash) {
  CallbackKeyHash hasher;
  CallbackKey mic{"alice", "mic-main"};
  CallbackKey cam{"alice", "cam-main"};
  CallbackKey bob{"bob", "mic-main"};

  EXPECT_NE(hasher(mic), hasher(cam));
  EXPECT_NE(hasher(mic), hasher(bob));
}

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyWorksAsUnorderedMapKey) {
  std::unordered_map<CallbackKey, int, CallbackKeyHash> map;

  CallbackKey k1{"alice", "mic-main"};
  CallbackKey k2{"bob", "cam-main"};
  CallbackKey k3{"alice", "cam-backup"};

  map[k1] = 1;
  map[k2] = 2;
  map[k3] = 3;

  EXPECT_EQ(map.size(), 3u);
  EXPECT_EQ(map[k1], 1);
  EXPECT_EQ(map[k2], 2);
  EXPECT_EQ(map[k3], 3);

  map[k1] = 42;
  EXPECT_EQ(map[k1], 42);
  EXPECT_EQ(map.size(), 3u);

  map.erase(k2);
  EXPECT_EQ(map.size(), 2u);
  EXPECT_EQ(map.count(k2), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, CallbackKeyEmptyIdentityWorks) {
  CallbackKey a{"", "mic-main"};
  CallbackKey b{"", "mic-main"};
  CallbackKeyHash hasher;
  EXPECT_TRUE(a == b);
  EXPECT_EQ(hasher(a), hasher(b));
}

// ============================================================================
// kMaxActiveReaders
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, MaxActiveReadersIs20) { EXPECT_EQ(maxActiveReaders(), 20); }

// ============================================================================
// Registration and clearing
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, SetAudioCallbackStoresRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, SetVideoCallbackStoresRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {});

  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearAudioCallbackRemovesRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
  ASSERT_EQ(audioCallbacks(dispatcher).size(), 1u);

  dispatcher.clearOnAudioFrameCallback("alice", "mic-main");
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearVideoCallbackRemovesRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {});
  ASSERT_EQ(videoCallbacks(dispatcher).size(), 1u);

  dispatcher.clearOnVideoFrameCallback("alice", "cam-main");
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearNonExistentCallbackIsNoOp) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_NO_THROW(dispatcher.clearOnAudioFrameCallback("nobody", "missing"));
  EXPECT_NO_THROW(dispatcher.clearOnVideoFrameCallback("nobody", "missing"));
}

TEST_F(SubscriptionThreadDispatcherTest, OverwriteAudioCallbackStoresTheNewCallback) {
  SubscriptionThreadDispatcher dispatcher;
  std::atomic<int> first{0};
  std::atomic<int> second{0};

  dispatcher.setOnAudioFrameCallback("alice", "mic-main", [&first](const AudioFrame&) { first++; });
  dispatcher.setOnAudioFrameCallback("alice", "mic-main", [&second](const AudioFrame&) { second++; });

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u) << "Re-registering with the same key should overwrite, not add";

  // Invoke what the slot actually holds: size alone would not catch a setter
  // that tore down the reader but forgot to install the new callback.
  const CallbackKey key{"alice", "mic-main"};
  audioCallbacks(dispatcher)[key].callback(makeAudioFrame());
  EXPECT_EQ(first.load(), 0) << "The replaced callback must not be the one stored";
  EXPECT_EQ(second.load(), 1);
}

TEST_F(SubscriptionThreadDispatcherTest, OverwriteVideoCallbackStoresTheNewCallback) {
  SubscriptionThreadDispatcher dispatcher;
  std::atomic<int> first{0};
  std::atomic<int> second{0};

  dispatcher.setOnVideoFrameCallback("alice", "cam-main", [&first](const VideoFrame&, std::int64_t) { first++; });
  dispatcher.setOnVideoFrameCallback("alice", "cam-main", [&second](const VideoFrame&, std::int64_t) { second++; });

  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);

  const CallbackKey key{"alice", "cam-main"};
  videoCallbacks(dispatcher)[key].legacy_callback(makeVideoFrame(), 0);
  EXPECT_EQ(first.load(), 0);
  EXPECT_EQ(second.load(), 1);
}

TEST_F(SubscriptionThreadDispatcherTest, OverwriteAudioCallbackStoresTheNewStreamOptions) {
  SubscriptionThreadDispatcher dispatcher;
  AudioStream::Options first_opts;
  first_opts.capacity = 4;
  AudioStream::Options second_opts;
  second_opts.capacity = 32;

  dispatcher.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {}, first_opts);
  dispatcher.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {}, second_opts);

  // The options travel with the callback into the next reader, so a stale copy
  // would silently rebuild the stream with the wrong queue behavior.
  const CallbackKey key{"alice", "mic-main"};
  EXPECT_EQ(audioCallbacks(dispatcher)[key].options.capacity, 32u);
}

TEST_F(SubscriptionThreadDispatcherTest, MultipleDistinctCallbacksAreIndependent) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
  dispatcher.setOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {});
  dispatcher.setOnAudioFrameCallback("bob", "mic-main", [](const AudioFrame&) {});
  dispatcher.setOnVideoFrameCallback("bob", "cam-main", [](const VideoFrame&, std::int64_t) {});

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 2u);
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 2u);

  dispatcher.clearOnAudioFrameCallback("alice", "mic-main");
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 2u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearingOneTrackNameDoesNotAffectOther) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
  dispatcher.setOnAudioFrameCallback("alice", "screenshare-main", [](const AudioFrame&) {});
  ASSERT_EQ(audioCallbacks(dispatcher).size(), 2u);

  dispatcher.clearOnAudioFrameCallback("alice", "mic-main");
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);

  CallbackKey remaining{"alice", "screenshare-main"};
  EXPECT_EQ(audioCallbacks(dispatcher).count(remaining), 1u);
}

// ============================================================================
// Active readers state (no real streams, just map state)
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, NoActiveReadersInitially) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_TRUE(activeReaders(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, ActiveReadersEmptyAfterCallbackRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
  EXPECT_TRUE(activeReaders(dispatcher).empty())
      << "Registering a callback without a subscribed track should not spawn "
         "readers";
}

TEST_F(SubscriptionThreadDispatcherTest, SubscribedTrackIsRetainedWithoutCallback) {
  SubscriptionThreadDispatcher dispatcher;
  auto track = std::make_shared<FakeMediaTrack>("fake-sid", TrackKind::KIND_AUDIO);

  dispatcher.handleTrackSubscribed("alice", "mic-main", track);

  const CallbackKey key{"alice", "mic-main"};
  ASSERT_EQ(subscribedTracks(dispatcher).count(key), 1u);
  EXPECT_EQ(subscribedTracks(dispatcher).at(key), track);
  EXPECT_TRUE(activeReaders(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, UnsubscribeRemovesRetainedTrack) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.handleTrackSubscribed("alice", "mic-main",
                                   std::make_shared<FakeMediaTrack>("fake-sid", TrackKind::KIND_AUDIO));

  dispatcher.handleTrackUnsubscribed("alice", TrackSource::SOURCE_MICROPHONE, "mic-main");

  EXPECT_TRUE(subscribedTracks(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, StopAllRemovesRetainedTracks) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.handleTrackSubscribed("alice", "mic-main",
                                   std::make_shared<FakeMediaTrack>("fake-sid", TrackKind::KIND_AUDIO));

  dispatcher.stopAll();

  EXPECT_TRUE(subscribedTracks(dispatcher).empty());
}

// ============================================================================
// Destruction safety
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, DestroyDispatcherWithRegisteredCallbacksIsSafe) {
  EXPECT_NO_THROW({
    SubscriptionThreadDispatcher dispatcher;
    dispatcher.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
    dispatcher.setOnVideoFrameCallback("bob", "cam-main", [](const VideoFrame&, std::int64_t) {});
  });
}

TEST_F(SubscriptionThreadDispatcherTest, DestroyDispatcherAfterClearingCallbacksIsSafe) {
  EXPECT_NO_THROW({
    SubscriptionThreadDispatcher dispatcher;
    dispatcher.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
    dispatcher.clearOnAudioFrameCallback("alice", "mic-main");
  });
}

// ============================================================================
// Thread-safety of registration/clearing
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, ConcurrentRegistrationDoesNotCrash) {
  SubscriptionThreadDispatcher dispatcher;
  constexpr int kThreads = 8;
  constexpr int kIterations = 100;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&dispatcher, t, kIterations]() {
      for (int i = 0; i < kIterations; ++i) {
        const std::string id = "participant-" + std::to_string(t);
        dispatcher.setOnAudioFrameCallback(id, "mic-main", [](const AudioFrame&) {});
        dispatcher.clearOnAudioFrameCallback(id, "mic-main");
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_TRUE(audioCallbacks(dispatcher).empty()) << "All callbacks should be cleared after concurrent register/clear";
}

TEST_F(SubscriptionThreadDispatcherTest, ConcurrentMixedAudioVideoRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  constexpr int kThreads = 4;
  constexpr int kIterations = 50;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&dispatcher, t, kIterations]() {
      const std::string id = "p-" + std::to_string(t);
      for (int i = 0; i < kIterations; ++i) {
        dispatcher.setOnAudioFrameCallback(id, "mic-main", [](const AudioFrame&) {});
        dispatcher.setOnVideoFrameCallback(id, "cam-main", [](const VideoFrame&, std::int64_t) {});
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(audioCallbacks(dispatcher).size(), static_cast<size_t>(kThreads));
  EXPECT_EQ(videoCallbacks(dispatcher).size(), static_cast<size_t>(kThreads));
}

// ============================================================================
// Bulk registration
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, ManyDistinctCallbacksCanBeRegistered) {
  SubscriptionThreadDispatcher dispatcher;
  constexpr int kCount = 50;

  for (int i = 0; i < kCount; ++i) {
    dispatcher.setOnAudioFrameCallback("participant-" + std::to_string(i), "mic-main", [](const AudioFrame&) {});
  }

  EXPECT_EQ(audioCallbacks(dispatcher).size(), static_cast<size_t>(kCount));

  for (int i = 0; i < kCount; ++i) {
    dispatcher.clearOnAudioFrameCallback("participant-" + std::to_string(i), "mic-main");
  }

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 0u);
}

// ============================================================================
// DataCallbackKey equality
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, DataCallbackKeyEqualKeysCompareEqual) {
  DataCallbackKey a{"alice", "my-track"};
  DataCallbackKey b{"alice", "my-track"};
  EXPECT_TRUE(a == b);
}

TEST_F(SubscriptionThreadDispatcherTest, DataCallbackKeyDifferentIdentityNotEqual) {
  DataCallbackKey a{"alice", "my-track"};
  DataCallbackKey b{"bob", "my-track"};
  EXPECT_FALSE(a == b);
}

TEST_F(SubscriptionThreadDispatcherTest, DataCallbackKeyDifferentTrackNameNotEqual) {
  DataCallbackKey a{"alice", "track-a"};
  DataCallbackKey b{"alice", "track-b"};
  EXPECT_FALSE(a == b);
}

// ============================================================================
// DataCallbackKeyHash
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, DataCallbackKeyHashEqualKeysProduceSameHash) {
  DataCallbackKey a{"alice", "my-track"};
  DataCallbackKey b{"alice", "my-track"};
  DataCallbackKeyHash hasher;
  EXPECT_EQ(hasher(a), hasher(b));
}

TEST_F(SubscriptionThreadDispatcherTest, DataCallbackKeyHashDifferentKeysLikelyDifferentHash) {
  DataCallbackKeyHash hasher;
  DataCallbackKey a{"alice", "track-a"};
  DataCallbackKey b{"alice", "track-b"};
  DataCallbackKey c{"bob", "track-a"};
  EXPECT_NE(hasher(a), hasher(b));
  EXPECT_NE(hasher(a), hasher(c));
}

TEST_F(SubscriptionThreadDispatcherTest, DataCallbackKeyWorksAsUnorderedMapKey) {
  std::unordered_map<DataCallbackKey, int, DataCallbackKeyHash> map;

  DataCallbackKey k1{"alice", "track-a"};
  DataCallbackKey k2{"bob", "track-b"};
  DataCallbackKey k3{"alice", "track-b"};

  map[k1] = 1;
  map[k2] = 2;
  map[k3] = 3;

  EXPECT_EQ(map.size(), 3u);
  EXPECT_EQ(map[k1], 1);
  EXPECT_EQ(map[k2], 2);
  EXPECT_EQ(map[k3], 3);

  map[k1] = 42;
  EXPECT_EQ(map[k1], 42);
  EXPECT_EQ(map.size(), 3u);

  map.erase(k2);
  EXPECT_EQ(map.size(), 2u);
  EXPECT_EQ(map.count(k2), 0u);
}

// ============================================================================
// Data callback registration and clearing
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, AddDataFrameCallbackStoresRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  auto id = dispatcher.addOnDataFrameCallback("alice", "my-track",
                                              [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});

  EXPECT_EQ(id, 0u);
  EXPECT_EQ(dataCallbacks(dispatcher).size(), 1u);

  // Add a second one to confirm size and IDs are correct
  auto id2 = dispatcher.addOnDataFrameCallback("alice", "my-track",
                                               [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
  EXPECT_EQ(id2, 1u);
  EXPECT_EQ(dataCallbacks(dispatcher).size(), 2u);
}

TEST_F(SubscriptionThreadDispatcherTest, RemoveDataFrameCallbackRemovesRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  auto id = dispatcher.addOnDataFrameCallback("alice", "my-track",
                                              [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
  ASSERT_EQ(dataCallbacks(dispatcher).size(), 1u);

  dispatcher.removeOnDataFrameCallback(id);
  EXPECT_EQ(dataCallbacks(dispatcher).size(), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, RemoveNonExistentDataCallbackIsNoOp) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_NO_THROW(dispatcher.removeOnDataFrameCallback(999));
}

TEST_F(SubscriptionThreadDispatcherTest, MultipleDataCallbacksForSameKeyAreIndependent) {
  SubscriptionThreadDispatcher dispatcher;
  auto cb = [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {};
  auto id1 = dispatcher.addOnDataFrameCallback("alice", "track", cb);
  auto id2 = dispatcher.addOnDataFrameCallback("alice", "track", cb);

  EXPECT_NE(id1, id2);
  EXPECT_EQ(dataCallbacks(dispatcher).size(), 2u);

  dispatcher.removeOnDataFrameCallback(id1);
  EXPECT_EQ(dataCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, DataCallbackIdsAreMonotonicallyIncreasing) {
  SubscriptionThreadDispatcher dispatcher;
  auto cb = [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {};
  auto id1 = dispatcher.addOnDataFrameCallback("alice", "t1", cb);
  auto id2 = dispatcher.addOnDataFrameCallback("bob", "t2", cb);
  auto id3 = dispatcher.addOnDataFrameCallback("carol", "t3", cb);

  EXPECT_LT(id1, id2);
  EXPECT_LT(id2, id3);
}

// ============================================================================
// Data track active readers (no real tracks, just map state)
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, NoActiveDataReadersInitially) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_TRUE(activeDataReaders(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, ActiveDataReadersEmptyAfterCallbackRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.addOnDataFrameCallback("alice", "my-track",
                                    [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
  EXPECT_TRUE(activeDataReaders(dispatcher).empty())
      << "Registering a callback without a published track should not spawn "
         "readers";
}

TEST_F(SubscriptionThreadDispatcherTest, NoRemoteDataTracksInitially) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_TRUE(remoteDataTracks(dispatcher).empty());
}

// ============================================================================
// Data reader replacement: cancellation and finished-state ownership
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, ActiveDataReaderNotCancelledByDefault) {
  auto reader = std::make_shared<ActiveDataReader>();
  EXPECT_FALSE(reader->cancelled.load());
  EXPECT_FALSE(reader->finished);
}

TEST_F(SubscriptionThreadDispatcherTest, ExtractDataReaderMarksCancelledAndRemovesEntry) {
  SubscriptionThreadDispatcher dispatcher;
  auto reader = std::make_shared<ActiveDataReader>();
  activeDataReaders(dispatcher)[0] = reader;

  auto extracted = extractDataReader(dispatcher, 0);

  EXPECT_TRUE(reader->cancelled.load()) << "Extract must cancel so an in-flight subscribe aborts";
  EXPECT_FALSE(extracted.joinable()) << "No real thread was attached to the seeded reader";
  EXPECT_TRUE(activeDataReaders(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, ExtractMissingDataReaderIsNoOp) {
  SubscriptionThreadDispatcher dispatcher;
  auto extracted = extractDataReader(dispatcher, 42);
  EXPECT_FALSE(extracted.joinable());
}

TEST_F(SubscriptionThreadDispatcherTest, MarkDataReaderFinishedSetsFlagAndDropsStream) {
  SubscriptionThreadDispatcher dispatcher;
  auto reader = std::make_shared<ActiveDataReader>();
  activeDataReaders(dispatcher)[0] = reader;

  markDataReaderFinished(reader);

  EXPECT_TRUE(reader->finished.load());
  EXPECT_EQ(reader->stream, nullptr);
  // The reader thread only marks itself; the dispatcher's slot is left for a
  // lifecycle path to reap, which is what lets a finished reader be replaced.
  ASSERT_EQ(activeDataReaders(dispatcher).size(), 1u);
  EXPECT_EQ(activeDataReaders(dispatcher)[0], reader);
}

TEST_F(SubscriptionThreadDispatcherTest, MarkDataReaderFinishedDoesNotTouchAReplacementInTheSameSlot) {
  SubscriptionThreadDispatcher dispatcher;
  auto original = std::make_shared<ActiveDataReader>();
  auto replacement = std::make_shared<ActiveDataReader>();
  activeDataReaders(dispatcher)[0] = replacement;

  // The original reader exited after being replaced; it marks only itself and
  // must not affect the newer reader that now owns the same callback id.
  markDataReaderFinished(original);

  EXPECT_TRUE(original->finished.load());
  ASSERT_EQ(activeDataReaders(dispatcher).size(), 1u);
  EXPECT_EQ(activeDataReaders(dispatcher)[0], replacement);
  EXPECT_FALSE(replacement->finished.load());
}

// Removing a data callback from inside its own callback reaches this extract
// on the reader's own thread. It must still cancel, close, and release the
// slot -- the thread is then detached by the caller instead of self-joined.
TEST_F(SubscriptionThreadDispatcherTest, ExtractDataReaderFromItsOwnThreadStillCancelsAndRemovesEntry) {
  SubscriptionThreadDispatcher dispatcher;
  auto reader = std::make_shared<ActiveDataReader>();
  reader->thread_id = std::this_thread::get_id();
  activeDataReaders(dispatcher)[3] = reader;

  auto extracted = extractDataReader(dispatcher, 3);

  EXPECT_TRUE(reader->cancelled.load()) << "A re-entrant removal must still stop delivery";
  EXPECT_TRUE(activeDataReaders(dispatcher).empty()) << "A re-entrant removal must still release the slot";
  EXPECT_FALSE(extracted.joinable());
}

TEST_F(SubscriptionThreadDispatcherTest, ExtractFinishedDataReaderRemovesEntryAndReturnsJoinableThread) {
  SubscriptionThreadDispatcher dispatcher;
  auto reader = std::make_shared<ActiveDataReader>();
  reader->finished = true;
  reader->thread = std::thread([]() {});
  activeDataReaders(dispatcher)[0] = reader;

  auto extracted = extractDataReader(dispatcher, 0);

  EXPECT_TRUE(reader->cancelled.load());
  EXPECT_TRUE(extracted.joinable());
  EXPECT_TRUE(activeDataReaders(dispatcher).empty());
  extracted.join();
}

// ============================================================================
// SID deduplication: audio/video reader start is skipped for the same SID
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, DuplicateSubscribeWithSameAudioSidDoesNotRestartReader) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  // Simulate an already-running reader for this subscription.
  const CallbackKey key{"alice", "mic"};
  activeReaders(dispatcher)[key].track_sid = "TR_audio_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  // A duplicate track_subscribed carrying the same SID must be a no-op: no
  // extract, no new stream/thread.
  auto track = std::make_shared<FakeMediaTrack>("TR_audio_1", TrackKind::KIND_AUDIO);
  dispatcher.handleTrackSubscribed("alice", "mic", track);

  EXPECT_EQ(activeReaderCount(dispatcher), 1u);
  EXPECT_EQ(activeReaders(dispatcher)[key].track_sid, "TR_audio_1");
  EXPECT_EQ(activeReaders(dispatcher)[key].audio_stream, nullptr) << "Reader must not have been rebuilt";
}

TEST_F(SubscriptionThreadDispatcherTest, DuplicateSubscribeWithSameVideoSidDoesNotRestartReader) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {});

  const CallbackKey key{"alice", "cam"};
  activeReaders(dispatcher)[key].track_sid = "TR_video_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  auto track = std::make_shared<FakeMediaTrack>("TR_video_1", TrackKind::KIND_VIDEO);
  dispatcher.handleTrackSubscribed("alice", "cam", track);

  EXPECT_EQ(activeReaderCount(dispatcher), 1u);
  EXPECT_EQ(activeReaders(dispatcher)[key].track_sid, "TR_video_1");
  EXPECT_EQ(activeReaders(dispatcher)[key].video_stream, nullptr) << "Reader must not have been rebuilt";
}

// ============================================================================
// setOn* replacement semantics: re-registering for a key with an active reader
// stops that reader in place so the next start binds the new callback.
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioWhileReaderActiveReplacesRegistrationAndStopsReader) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  // Simulate an already-running reader for this subscription.
  const CallbackKey key{"alice", "mic"};
  activeReaders(dispatcher)[key].track_sid = "TR_audio_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  std::atomic<int> replacement_invocations{0};
  dispatcher.setOnAudioFrameCallback("alice", "mic",
                                     [&replacement_invocations](const AudioFrame&) { replacement_invocations++; });

  EXPECT_EQ(activeReaderCount(dispatcher), 0u) << "The stale reader must be extracted so it stops dispatching to the "
                                                  "callback it captured by value";
  ASSERT_EQ(audioCallbacks(dispatcher).size(), 1u);
  audioCallbacks(dispatcher)[key].callback(makeAudioFrame());
  EXPECT_EQ(replacement_invocations.load(), 1) << "The replacement callback must be the one now stored";
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnVideoWhileReaderActiveReplacesRegistrationAndStopsReader) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {});

  const CallbackKey key{"alice", "cam"};
  activeReaders(dispatcher)[key].track_sid = "TR_video_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  std::atomic<int> replacement_invocations{0};
  dispatcher.setOnVideoFrameCallback(
      "alice", "cam", [&replacement_invocations](const VideoFrame&, std::int64_t) { replacement_invocations++; });

  EXPECT_EQ(activeReaderCount(dispatcher), 0u);
  ASSERT_EQ(videoCallbacks(dispatcher).size(), 1u);
  videoCallbacks(dispatcher)[key].legacy_callback(makeVideoFrame(), 0);
  EXPECT_EQ(replacement_invocations.load(), 1) << "The replacement callback must be the one now stored";
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnVideoEventWhileReaderActiveReplacesRegistrationAndStopsReader) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {});

  const CallbackKey key{"alice", "cam"};
  activeReaders(dispatcher)[key].track_sid = "TR_video_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  // The legacy and event callbacks share one registration slot, so registering
  // the event variant must displace the legacy one and stop its reader.
  dispatcher.setOnVideoFrameEventCallback("alice", "cam", [](const VideoFrameEvent&) {});

  EXPECT_EQ(activeReaderCount(dispatcher), 0u);
  ASSERT_EQ(videoCallbacks(dispatcher).size(), 1u);
  EXPECT_FALSE(static_cast<bool>(videoCallbacks(dispatcher)[key].legacy_callback));
  EXPECT_TRUE(static_cast<bool>(videoCallbacks(dispatcher)[key].event_callback));
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioAfterReplacementRestartsOnNextSubscribe) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  const CallbackKey key{"alice", "mic"};
  activeReaders(dispatcher)[key].track_sid = "TR_audio_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  // Replacing extracts the reader, so the SID dedup guard no longer suppresses a
  // restart for the same publication -- this is what lets Room rebuild the reader
  // against the new callback.
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  ASSERT_EQ(activeReaderCount(dispatcher), 0u);

  // Re-subscribing the same SID now reaches stream construction instead of being
  // short-circuited by the guard. The fake track carries an invalid FFI handle,
  // so AudioStream::fromTrack throws -- that throw is precisely the evidence
  // that startup was attempted rather than skipped.
  auto track = std::make_shared<FakeMediaTrack>("TR_audio_1", TrackKind::KIND_AUDIO);
  EXPECT_ANY_THROW(dispatcher.handleTrackSubscribed("alice", "mic", track));

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnVideoAfterReplacementRestartsOnNextSubscribe) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {});

  const CallbackKey key{"alice", "cam"};
  activeReaders(dispatcher)[key].track_sid = "TR_video_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  dispatcher.setOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {});
  ASSERT_EQ(activeReaderCount(dispatcher), 0u);

  // As in the audio case, the throw from VideoStream::fromTrack on the invalid
  // fake handle is the evidence that the guard no longer short-circuits startup.
  auto track = std::make_shared<FakeMediaTrack>("TR_video_1", TrackKind::KIND_VIDEO);
  EXPECT_ANY_THROW(dispatcher.handleTrackSubscribed("alice", "cam", track));

  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioWithoutReplacementLeavesSidGuardIntact) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  const CallbackKey key{"alice", "mic"};
  activeReaders(dispatcher)[key].track_sid = "TR_audio_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  // Counterpart to the test above: with the reader still in place, a duplicate
  // subscribe for the same SID is skipped and never reaches stream construction.
  auto track = std::make_shared<FakeMediaTrack>("TR_audio_1", TrackKind::KIND_AUDIO);
  EXPECT_NO_THROW(dispatcher.handleTrackSubscribed("alice", "mic", track));
  EXPECT_EQ(activeReaderCount(dispatcher), 1u);
}

// Distinct from ClearAudioCallbackRemovesRegistration, which clears a key that
// has no reader: this covers clearing while a reader is active.
TEST_F(SubscriptionThreadDispatcherTest, ClearAudioCallbackWithActiveReaderStopsReader) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  const CallbackKey key{"alice", "mic"};
  activeReaders(dispatcher)[key].track_sid = "TR_audio_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  dispatcher.clearOnAudioFrameCallback("alice", "mic");
  EXPECT_EQ(activeReaderCount(dispatcher), 0u);
  EXPECT_TRUE(audioCallbacks(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, ClearVideoCallbackWithActiveReaderStopsReader) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {});

  const CallbackKey key{"alice", "cam"};
  activeReaders(dispatcher)[key].track_sid = "TR_video_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  dispatcher.clearOnVideoFrameCallback("alice", "cam");
  EXPECT_EQ(activeReaderCount(dispatcher), 0u);
  EXPECT_TRUE(videoCallbacks(dispatcher).empty());
}

// The reverse of SetOnVideoEventWhileReaderActiveReplacesRegistrationAndStopsReader:
// the legacy setter must displace a stored event callback, not merge with it.
TEST_F(SubscriptionThreadDispatcherTest, SetOnVideoDisplacesStoredEventCallback) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameEventCallback("alice", "cam", [](const VideoFrameEvent&) {});

  const CallbackKey key{"alice", "cam"};
  activeReaders(dispatcher)[key].track_sid = "TR_video_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  dispatcher.setOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {});

  EXPECT_EQ(activeReaderCount(dispatcher), 0u);
  ASSERT_EQ(videoCallbacks(dispatcher).size(), 1u);
  EXPECT_TRUE(static_cast<bool>(videoCallbacks(dispatcher)[key].legacy_callback));
  EXPECT_FALSE(static_cast<bool>(videoCallbacks(dispatcher)[key].event_callback));
}

// Replacement must be scoped to its own key; an unrelated subscription's reader
// is extracted by key, so a bug there would tear down the wrong stream.
TEST_F(SubscriptionThreadDispatcherTest, ReplacementLeavesOtherKeysReadersUntouched) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  dispatcher.setOnAudioFrameCallback("bob", "mic", [](const AudioFrame&) {});

  const CallbackKey alice{"alice", "mic"};
  const CallbackKey bob{"bob", "mic"};
  activeReaders(dispatcher)[alice].track_sid = "TR_audio_1";
  activeReaders(dispatcher)[bob].track_sid = "TR_audio_2";
  ASSERT_EQ(activeReaderCount(dispatcher), 2u);

  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  EXPECT_EQ(activeReaderCount(dispatcher), 1u);
  EXPECT_EQ(activeReaders(dispatcher).count(alice), 0u);
  ASSERT_EQ(activeReaders(dispatcher).count(bob), 1u);
  EXPECT_EQ(activeReaders(dispatcher)[bob].track_sid, "TR_audio_2") << "Replacing one key must not disturb another";
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 2u);
}

// Unsubscribe stops the reader but keeps the registration, so a replacement made
// while unsubscribed is the one that binds on the next subscribe.
TEST_F(SubscriptionThreadDispatcherTest, ReplacementWhileUnsubscribedKeepsRegistrationForNextSubscribe) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  const CallbackKey key{"alice", "mic"};
  activeReaders(dispatcher)[key].track_sid = "TR_audio_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  dispatcher.handleTrackUnsubscribed("alice", TrackSource::SOURCE_MICROPHONE, "mic");
  EXPECT_EQ(activeReaderCount(dispatcher), 0u);
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u) << "Unsubscribe must preserve the registration";

  std::atomic<int> replacement_invocations{0};
  dispatcher.setOnAudioFrameCallback("alice", "mic",
                                     [&replacement_invocations](const AudioFrame&) { replacement_invocations++; });
  ASSERT_EQ(audioCallbacks(dispatcher).size(), 1u);
  audioCallbacks(dispatcher)[key].callback(makeAudioFrame());
  EXPECT_EQ(replacement_invocations.load(), 1);
}

// ============================================================================
// Self-join detection
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, IsSelfThreadIdentifiesTheCallingThread) {
  EXPECT_TRUE(isSelfThread(std::this_thread::get_id()));
  EXPECT_FALSE(isSelfThread(std::thread::id{})) << "A default-constructed id must never match a running thread";

  std::thread other([]() {});
  const auto other_id = other.get_id();
  other.join();
  EXPECT_FALSE(isSelfThread(other_id));
}

// ============================================================================
// SID deduplication: data reader start is skipped for the same SID and
// replaced (stopping the previous reader) for a new SID
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, DuplicateDataPublishWithSameSidDoesNotRestartReader) {
  SubscriptionThreadDispatcher dispatcher;
  auto reader = std::make_shared<ActiveDataReader>();
  reader->remote_track = RemoteDataTrackTestAccess::create({"foo", "TR_data_1", false}, "alice");
  activeDataReaders(dispatcher)[7] = reader;

  auto incoming = RemoteDataTrackTestAccess::create({"foo", "TR_data_1", false}, "alice");
  auto old_thread = startDataReader(dispatcher, 7, DataCallbackKey{"alice", "foo"}, incoming);

  EXPECT_FALSE(old_thread.joinable());
  EXPECT_EQ(activeDataReaderCount(dispatcher), 1u);
  EXPECT_EQ(activeDataReaders(dispatcher)[7], reader) << "Same-SID publish must not replace the reader";
  EXPECT_FALSE(reader->cancelled.load()) << "A skipped reader must not be cancelled";
}

TEST_F(SubscriptionThreadDispatcherTest, FinishedDataReaderWithSameSidIsReplaced) {
  SubscriptionThreadDispatcher dispatcher;
  auto reader = std::make_shared<ActiveDataReader>();
  reader->remote_track = RemoteDataTrackTestAccess::create({"foo", "TR_data_1", false}, "alice");
  reader->finished = true;
  activeDataReaders(dispatcher)[7] = reader;

  auto incoming = RemoteDataTrackTestAccess::create({"foo", "TR_data_1", false}, "alice");
  auto old_thread = startDataReader(dispatcher, 7, DataCallbackKey{"alice", "foo"}, incoming);
  if (old_thread.joinable()) {
    old_thread.join();
  }

  EXPECT_TRUE(reader->cancelled.load()) << "Finished reader must be extracted before replacement";
  EXPECT_TRUE(waitFor(
      [&] {
        return activeDataReaderCount(dispatcher) == 1u && activeDataReaders(dispatcher)[7] != reader &&
               activeDataReaders(dispatcher)[7]->finished;
      },
      2s));

  dispatcher.stopAll();
  EXPECT_TRUE(activeDataReaders(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, RepublishWithNewDataSidStopsPreviousReader) {
  SubscriptionThreadDispatcher dispatcher;
  auto previous = std::make_shared<ActiveDataReader>();
  previous->remote_track = RemoteDataTrackTestAccess::create({"foo", "TR_data_1", false}, "alice");
  activeDataReaders(dispatcher)[7] = previous;

  // A republish under the same (participant, name) but a NEW SID must stop the
  // previous reader and start a fresh one.
  auto republished = RemoteDataTrackTestAccess::create({"foo", "TR_data_2", false}, "alice");
  auto old_thread = startDataReader(dispatcher, 7, DataCallbackKey{"alice", "foo"}, republished);
  if (old_thread.joinable()) {
    old_thread.join();
  }

  EXPECT_TRUE(previous->cancelled.load()) << "Previous reader must be cancelled on republish";

  // The replacement reader has an invalid FFI handle, so its subscribe fails
  // fast and marks itself finished while the dispatcher keeps ownership.
  EXPECT_TRUE(waitFor(
      [&] { return activeDataReaderCount(dispatcher) == 1u && activeDataReaders(dispatcher)[7]->finished; }, 2s));

  dispatcher.stopAll();
  EXPECT_TRUE(activeDataReaders(dispatcher).empty());
}

// ============================================================================
// Data track destruction safety
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, DestroyDispatcherWithDataCallbacksIsSafe) {
  EXPECT_NO_THROW({
    SubscriptionThreadDispatcher dispatcher;
    dispatcher.addOnDataFrameCallback("alice", "track-a",
                                      [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
    dispatcher.addOnDataFrameCallback("bob", "track-b",
                                      [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
  });
}

TEST_F(SubscriptionThreadDispatcherTest, DestroyDispatcherAfterRemovingDataCallbacksIsSafe) {
  EXPECT_NO_THROW({
    SubscriptionThreadDispatcher dispatcher;
    auto id = dispatcher.addOnDataFrameCallback("alice", "track-a",
                                                [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
    dispatcher.removeOnDataFrameCallback(id);
  });
}

// ============================================================================
// Mixed audio/video/data registration
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, MixedAudioVideoDataCallbacksAreIndependent) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
  dispatcher.setOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {});
  dispatcher.addOnDataFrameCallback("alice", "data-track",
                                    [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
  EXPECT_EQ(dataCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, StopAllClearsDataCallbacksAndReaders) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.addOnDataFrameCallback("alice", "track-a",
                                    [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
  dispatcher.addOnDataFrameCallback("bob", "track-b",
                                    [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});

  dispatcher.stopAll();

  EXPECT_EQ(dataCallbacks(dispatcher).size(), 0u);
  EXPECT_TRUE(activeDataReaders(dispatcher).empty());
  EXPECT_TRUE(remoteDataTracks(dispatcher).empty());
}

// ============================================================================
// Concurrent data callback registration
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, ConcurrentDataCallbackRegistrationDoesNotCrash) {
  SubscriptionThreadDispatcher dispatcher;
  constexpr int kThreads = 8;
  constexpr int kIterations = 100;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&dispatcher, t, kIterations]() {
      for (int i = 0; i < kIterations; ++i) {
        auto id =
            dispatcher.addOnDataFrameCallback("participant-" + std::to_string(t), "track",
                                              [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
        dispatcher.removeOnDataFrameCallback(id);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_TRUE(dataCallbacks(dispatcher).empty()) << "All data callbacks should be cleared after concurrent "
                                                    "register/remove";
}

// ============================================================================
// Late registration (GitHub issue #235)
//
// A callback registered after the track_subscribed event -- e.g. from a GUI
// thread once RoomDelegate::onTrackSubscribed has returned -- must start a
// reader from the retained subscription instead of waiting for an event that
// will never come again.
//
// The fake track carries an invalid FFI handle, so AudioStream::fromTrack /
// VideoStream::fromTrack throw when reader startup is attempted. That throw
// is the deterministic evidence that startup was reached rather than skipped;
// a no-throw means the dispatcher never tried to start a reader.
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioAfterTrackSubscribedStartsReaderImmediately) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.handleTrackSubscribed("alice", "mic",
                                   std::make_shared<FakeMediaTrack>("TR_audio_1", TrackKind::KIND_AUDIO));
  ASSERT_TRUE(activeReaders(dispatcher).empty()) << "No callback registered yet, so no reader";

  EXPECT_ANY_THROW(dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}))
      << "Registering after the subscription must attempt to start a reader right away";
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u) << "The registration must survive a failed startup";
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnVideoAfterTrackSubscribedStartsReaderImmediately) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.handleTrackSubscribed("alice", "cam",
                                   std::make_shared<FakeMediaTrack>("TR_video_1", TrackKind::KIND_VIDEO));
  ASSERT_TRUE(activeReaders(dispatcher).empty());

  EXPECT_ANY_THROW(dispatcher.setOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {}))
      << "Registering after the subscription must attempt to start a reader right away";
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnVideoEventAfterTrackSubscribedStartsReaderImmediately) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.handleTrackSubscribed("alice", "cam",
                                   std::make_shared<FakeMediaTrack>("TR_video_1", TrackKind::KIND_VIDEO));
  ASSERT_TRUE(activeReaders(dispatcher).empty());

  EXPECT_ANY_THROW(dispatcher.setOnVideoFrameEventCallback("alice", "cam", [](const VideoFrameEvent&) {}))
      << "Registering after the subscription must attempt to start a reader right away";
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
}

// The counterpart: registering first and subscribing second must also start
// exactly once, on the subscribe. Together with the tests above this pins both
// orderings the issue contrasts.
TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioBeforeTrackSubscribedStartsReaderOnSubscribe) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_NO_THROW(dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}))
      << "Nothing is subscribed yet, so registration must not try to start a reader";
  EXPECT_TRUE(activeReaders(dispatcher).empty());

  EXPECT_ANY_THROW(dispatcher.handleTrackSubscribed(
      "alice", "mic", std::make_shared<FakeMediaTrack>("TR_audio_1", TrackKind::KIND_AUDIO)))
      << "The subscribe event must start the reader for the pending registration";
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioAfterVideoTrackSubscribedDoesNotStartAReader) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.handleTrackSubscribed("alice", "cam",
                                   std::make_shared<FakeMediaTrack>("TR_video_1", TrackKind::KIND_VIDEO));

  // The retained track is video and only an audio callback exists: nothing to
  // bind, so startup must be skipped rather than attempted against the wrong
  // kind.
  EXPECT_NO_THROW(dispatcher.setOnAudioFrameCallback("alice", "cam", [](const AudioFrame&) {}));
  EXPECT_TRUE(activeReaders(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioAfterUnsubscribeDoesNotStartAReader) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.handleTrackSubscribed("alice", "mic",
                                   std::make_shared<FakeMediaTrack>("TR_audio_1", TrackKind::KIND_AUDIO));
  dispatcher.handleTrackUnsubscribed("alice", TrackSource::SOURCE_MICROPHONE, "mic");

  EXPECT_NO_THROW(dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}))
      << "Unsubscribe must drop the retained track so late registration defers again";
  EXPECT_TRUE(activeReaders(dispatcher).empty());
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u) << "The registration is kept for the next subscribe";
}

// ============================================================================
// Resubscribe with a new SID (republish under the same track name)
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, ResubscribeWithNewSidStopsPreviousReaderAndRestarts) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  const CallbackKey key{"alice", "mic"};
  activeReaders(dispatcher)[key].track_sid = "TR_audio_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  // A different SID is a new publication: the stale reader must go and a fresh
  // start must be attempted (the throw is that attempt).
  EXPECT_ANY_THROW(dispatcher.handleTrackSubscribed(
      "alice", "mic", std::make_shared<FakeMediaTrack>("TR_audio_2", TrackKind::KIND_AUDIO)));

  EXPECT_TRUE(activeReaders(dispatcher).empty()) << "The reader for the old SID must have been extracted";
  ASSERT_EQ(subscribedTracks(dispatcher).count(key), 1u);
  EXPECT_EQ(subscribedTracks(dispatcher).at(key)->sid(), "TR_audio_2");
  EXPECT_TRUE(drainingReaders(dispatcher).empty());
}

// ============================================================================
// Drain-before-restart
//
// Replacing, clearing, unsubscribing, and resubscribing all stop the previous
// reader, join it with the lock released, and only then start a replacement.
// While that join is in progress the key is marked draining and nothing may
// start a reader for it, so the old and new callbacks never run concurrently.
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, ReaderStartIsDeferredWhileKeyIsDraining) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  const CallbackKey key{"alice", "mic"};
  drainingReaders(dispatcher)[key] = 1;

  // With a registration and a subscribed track this would normally attempt a
  // start (and throw). A draining key must defer instead.
  EXPECT_NO_THROW(dispatcher.handleTrackSubscribed(
      "alice", "mic", std::make_shared<FakeMediaTrack>("TR_audio_1", TrackKind::KIND_AUDIO)));
  EXPECT_TRUE(activeReaders(dispatcher).empty());
  EXPECT_EQ(subscribedTracks(dispatcher).count(key), 1u) << "The subscription is retained for the drain owner";
  EXPECT_EQ(drainingReaders(dispatcher).at(key), 1) << "A caller that did not drain must not clear the mark";

  // Once the drain owner clears the mark, the deferred start goes ahead.
  drainingReaders(dispatcher).clear();
  EXPECT_ANY_THROW(dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}));
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioJoinsPreviousReaderAndClearsDrainMark) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  const CallbackKey key{"alice", "mic"};
  std::atomic<bool> previous_ran{false};
  seedJoinableReader(dispatcher, key, previous_ran);

  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  EXPECT_TRUE(previous_ran.load()) << "The setter must have joined the previous reader before returning";
  EXPECT_TRUE(activeReaders(dispatcher).empty()) << "No subscribed track, so nothing restarts";
  EXPECT_TRUE(drainingReaders(dispatcher).empty()) << "The drain mark must be cleared once the join completes";
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnVideoJoinsPreviousReaderAndClearsDrainMark) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {});

  const CallbackKey key{"alice", "cam"};
  std::atomic<bool> previous_ran{false};
  seedJoinableReader(dispatcher, key, previous_ran);

  dispatcher.setOnVideoFrameEventCallback("alice", "cam", [](const VideoFrameEvent&) {});

  EXPECT_TRUE(previous_ran.load());
  EXPECT_TRUE(activeReaders(dispatcher).empty());
  EXPECT_TRUE(drainingReaders(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, ClearAudioCallbackJoinsPreviousReaderAndClearsDrainMark) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  const CallbackKey key{"alice", "mic"};
  std::atomic<bool> previous_ran{false};
  seedJoinableReader(dispatcher, key, previous_ran);

  dispatcher.clearOnAudioFrameCallback("alice", "mic");

  EXPECT_TRUE(previous_ran.load());
  EXPECT_TRUE(activeReaders(dispatcher).empty());
  EXPECT_TRUE(drainingReaders(dispatcher).empty());
  EXPECT_TRUE(audioCallbacks(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, HandleTrackUnsubscribedJoinsPreviousReaderAndClearsDrainMark) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  const CallbackKey key{"alice", "mic"};
  std::atomic<bool> previous_ran{false};
  seedJoinableReader(dispatcher, key, previous_ran);

  dispatcher.handleTrackUnsubscribed("alice", TrackSource::SOURCE_MICROPHONE, "mic");

  EXPECT_TRUE(previous_ran.load());
  EXPECT_TRUE(activeReaders(dispatcher).empty());
  EXPECT_TRUE(drainingReaders(dispatcher).empty());
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u) << "Unsubscribe keeps the registration";
}

// A subscribe event that arrives for the key while it is draining is retained
// and honoured by the drain owner's restart, not lost.
TEST_F(SubscriptionThreadDispatcherTest, DrainOwnerRestartsReaderSubscribedDuringDrain) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  const CallbackKey key{"alice", "mic"};
  drainingReaders(dispatcher)[key] = 1;
  dispatcher.handleTrackSubscribed("alice", "mic",
                                   std::make_shared<FakeMediaTrack>("TR_audio_1", TrackKind::KIND_AUDIO));
  ASSERT_TRUE(activeReaders(dispatcher).empty()) << "Deferred while draining";

  // Hand the drain to a lifecycle call: seed a joinable reader so the clear
  // both drains and, on completion, decrements the mark we planted plus its
  // own. The subsequent restart then reaches stream construction (throws).
  drainingReaders(dispatcher).clear();
  std::atomic<bool> previous_ran{false};
  seedJoinableReader(dispatcher, key, previous_ran, "TR_audio_0");
  EXPECT_ANY_THROW(dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}));
  EXPECT_TRUE(previous_ran.load()) << "The previous reader must be joined before the restart is attempted";
  EXPECT_TRUE(drainingReaders(dispatcher).empty());
}

// ============================================================================
// Self-join avoidance
//
// Every lifecycle path disposes of stopped reader threads through one helper.
// Called from any other thread it joins; called from the reader's own thread
// (a re-entrant call from inside a frame callback, or Room::disconnect() from
// inside a data callback) it detaches, because a self-join throws
// std::system_error and a joinable std::thread destroyed during the unwind
// terminates the process.
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, DisposeReaderThreadJoinsWhenCalledFromAnotherThread) {
  SubscriptionThreadDispatcher dispatcher;
  std::atomic<bool> ran{false};
  std::thread worker([&ran]() { ran.store(true); });

  disposeReaderThread(dispatcher, std::move(worker));

  EXPECT_TRUE(ran.load()) << "Dispose from another thread must join, guaranteeing the thread finished";
  EXPECT_FALSE(worker.joinable());
}

TEST_F(SubscriptionThreadDispatcherTest, DisposeReaderThreadDetachesWhenCalledFromThatThread) {
  SubscriptionThreadDispatcher dispatcher;
  std::promise<void> ready;
  std::promise<void> done;
  auto done_future = done.get_future();
  auto holder = std::make_shared<std::thread>();

  *holder = std::thread([&dispatcher, holder, ready = ready.get_future().share(), &done]() {
    ready.wait(); // *holder now refers to this very thread
    EXPECT_NO_THROW(disposeReaderThread(dispatcher, std::move(*holder)));
    EXPECT_FALSE(holder->joinable()) << "A self-dispose must detach, leaving nothing joinable to destroy";
    done.set_value();
  });
  ready.set_value();

  ASSERT_EQ(done_future.wait_for(5s), std::future_status::ready) << "Self-dispose hung instead of detaching";
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioFromInsideItsOwnReaderDetachesAndReplaces) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  const CallbackKey key{"alice", "mic"};
  std::promise<void> ready;
  std::promise<void> done;
  auto done_future = done.get_future();
  std::atomic<int> replacement_invocations{0};

  seedSelfCallingReader(dispatcher, key, ready.get_future().share(), [&]() {
    // What a frame callback that re-registers itself does.
    dispatcher.setOnAudioFrameCallback(
        "alice", "mic", [&replacement_invocations](const AudioFrame&) { replacement_invocations.fetch_add(1); });
    done.set_value();
  });
  ready.set_value();

  ASSERT_EQ(done_future.wait_for(5s), std::future_status::ready)
      << "Re-entrant setOnAudioFrameCallback never returned; the reader self-joined";
  EXPECT_TRUE(activeReaders(dispatcher).empty());
  EXPECT_TRUE(drainingReaders(dispatcher).empty());
  ASSERT_EQ(audioCallbacks(dispatcher).size(), 1u);
  audioCallbacks(dispatcher)[key].callback(makeAudioFrame());
  EXPECT_EQ(replacement_invocations.load(), 1) << "The re-entrant registration must still take effect";
}

TEST_F(SubscriptionThreadDispatcherTest, ClearOnAudioFromInsideItsOwnReaderDetaches) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  const CallbackKey key{"alice", "mic"};
  std::promise<void> ready;
  std::promise<void> done;
  auto done_future = done.get_future();

  seedSelfCallingReader(dispatcher, key, ready.get_future().share(), [&]() {
    dispatcher.clearOnAudioFrameCallback("alice", "mic");
    done.set_value();
  });
  ready.set_value();

  ASSERT_EQ(done_future.wait_for(5s), std::future_status::ready)
      << "Re-entrant clearOnAudioFrameCallback never returned; the reader self-joined";
  EXPECT_TRUE(activeReaders(dispatcher).empty());
  EXPECT_TRUE(audioCallbacks(dispatcher).empty());
  EXPECT_TRUE(drainingReaders(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, RemoveDataCallbackFromInsideItsOwnReaderDetachesAndStopsIt) {
  SubscriptionThreadDispatcher dispatcher;
  const auto id = dispatcher.addOnDataFrameCallback(
      "alice", "data", [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});

  std::promise<void> ready;
  std::promise<void> done;
  auto done_future = done.get_future();
  auto reader = std::make_shared<ActiveDataReader>();
  reader->thread = std::thread([&dispatcher, id, ready = ready.get_future().share(), &done]() {
    ready.wait();
    dispatcher.removeOnDataFrameCallback(id); // from inside "its own" data callback
    done.set_value();
  });
  reader->thread_id = reader->thread.get_id();
  activeDataReaders(dispatcher)[id] = reader;
  ready.set_value();

  ASSERT_EQ(done_future.wait_for(5s), std::future_status::ready)
      << "Re-entrant removeOnDataFrameCallback never returned; the data reader self-joined";
  EXPECT_TRUE(reader->cancelled.load()) << "The removal must stop the reader, not just refuse";
  EXPECT_TRUE(activeDataReaders(dispatcher).empty());
  EXPECT_TRUE(dataCallbacks(dispatcher).empty());
}

// Room::disconnect() from inside a data frame callback reaches stopAll() on the
// reader's own thread. Before the fix this self-joined, threw, and the joinable
// std::thread destroyed during unwinding took the whole process down.
TEST_F(SubscriptionThreadDispatcherTest, StopAllFromInsideDataReaderThreadDetachesInsteadOfSelfJoining) {
  SubscriptionThreadDispatcher dispatcher;
  std::promise<void> ready;
  std::promise<void> done;
  auto done_future = done.get_future();
  auto reader = std::make_shared<ActiveDataReader>();
  reader->thread = std::thread([&dispatcher, ready = ready.get_future().share(), &done]() {
    ready.wait();
    EXPECT_NO_THROW(dispatcher.stopAll());
    done.set_value();
  });
  reader->thread_id = reader->thread.get_id();
  activeDataReaders(dispatcher)[9] = reader;
  ready.set_value();

  ASSERT_EQ(done_future.wait_for(5s), std::future_status::ready) << "stopAll() from a data reader thread hung";
  EXPECT_TRUE(reader->cancelled.load());
  EXPECT_TRUE(activeDataReaders(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, StopAllFromInsideMediaReaderThreadDetachesInsteadOfSelfJoining) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {});

  std::promise<void> ready;
  std::promise<void> done;
  auto done_future = done.get_future();
  seedSelfCallingReader(dispatcher, CallbackKey{"alice", "cam"}, ready.get_future().share(), [&]() {
    EXPECT_NO_THROW(dispatcher.stopAll());
    done.set_value();
  });
  ready.set_value();

  ASSERT_EQ(done_future.wait_for(5s), std::future_status::ready) << "stopAll() from a media reader thread hung";
  EXPECT_TRUE(activeReaders(dispatcher).empty());
  EXPECT_TRUE(videoCallbacks(dispatcher).empty());
}

} // namespace livekit

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
