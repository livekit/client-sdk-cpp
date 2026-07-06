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
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../common/remote_data_track_test_access.h"

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
  static auto& dataCallbacks(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.data_callbacks_; }
  static auto& activeDataReaders(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.active_data_readers_; }
  static auto& remoteDataTracks(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.remote_data_tracks_; }
  static int maxActiveReaders() { return SubscriptionThreadDispatcher::kMaxActiveReaders; }
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

  static void markDataReaderFinishedIfCurrent(SubscriptionThreadDispatcher& dispatcher, DataFrameCallbackId id,
                                              const std::shared_ptr<ActiveDataReader>& reader) {
    dispatcher.markDataReaderFinishedIfCurrent(id, reader);
  }

  static std::thread startDataReader(SubscriptionThreadDispatcher& dispatcher, DataFrameCallbackId id,
                                     const DataCallbackKey& key, const std::shared_ptr<RemoteDataTrack>& track) {
    const std::scoped_lock<std::mutex> lock(dispatcher.lock_);
    return dispatcher.startDataReaderLocked(id, key, track,
                                            [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
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
  EXPECT_TRUE(dispatcher.trySetOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {}));

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, SetVideoCallbackStoresRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_TRUE(dispatcher.trySetOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {}));

  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearAudioCallbackRemovesRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  (void)dispatcher.trySetOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
  ASSERT_EQ(audioCallbacks(dispatcher).size(), 1u);

  dispatcher.clearOnAudioFrameCallback("alice", "mic-main");
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearVideoCallbackRemovesRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  (void)dispatcher.trySetOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {});
  ASSERT_EQ(videoCallbacks(dispatcher).size(), 1u);

  dispatcher.clearOnVideoFrameCallback("alice", "cam-main");
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearNonExistentCallbackIsNoOp) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_NO_THROW(dispatcher.clearOnAudioFrameCallback("nobody", "missing"));
  EXPECT_NO_THROW(dispatcher.clearOnVideoFrameCallback("nobody", "missing"));
}

TEST_F(SubscriptionThreadDispatcherTest, OverwriteAudioCallbackKeepsSingleEntry) {
  SubscriptionThreadDispatcher dispatcher;
  std::atomic<int> counter1{0};
  std::atomic<int> counter2{0};

  (void)dispatcher.trySetOnAudioFrameCallback("alice", "mic-main", [&counter1](const AudioFrame&) { counter1++; });
  (void)dispatcher.trySetOnAudioFrameCallback("alice", "mic-main", [&counter2](const AudioFrame&) { counter2++; });

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u) << "Re-registering with the same key should overwrite, not add";
}

TEST_F(SubscriptionThreadDispatcherTest, OverwriteVideoCallbackKeepsSingleEntry) {
  SubscriptionThreadDispatcher dispatcher;
  (void)dispatcher.trySetOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {});
  (void)dispatcher.trySetOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {});

  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, MultipleDistinctCallbacksAreIndependent) {
  SubscriptionThreadDispatcher dispatcher;
  (void)dispatcher.trySetOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
  (void)dispatcher.trySetOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {});
  (void)dispatcher.trySetOnAudioFrameCallback("bob", "mic-main", [](const AudioFrame&) {});
  (void)dispatcher.trySetOnVideoFrameCallback("bob", "cam-main", [](const VideoFrame&, std::int64_t) {});

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 2u);
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 2u);

  dispatcher.clearOnAudioFrameCallback("alice", "mic-main");
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 2u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearingOneTrackNameDoesNotAffectOther) {
  SubscriptionThreadDispatcher dispatcher;
  (void)dispatcher.trySetOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
  (void)dispatcher.trySetOnAudioFrameCallback("alice", "screenshare-main", [](const AudioFrame&) {});
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
  (void)dispatcher.trySetOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
  EXPECT_TRUE(activeReaders(dispatcher).empty())
      << "Registering a callback without a subscribed track should not spawn "
         "readers";
}

// ============================================================================
// Destruction safety
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, DestroyDispatcherWithRegisteredCallbacksIsSafe) {
  EXPECT_NO_THROW({
    SubscriptionThreadDispatcher dispatcher;
    (void)dispatcher.trySetOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
    (void)dispatcher.trySetOnVideoFrameCallback("bob", "cam-main", [](const VideoFrame&, std::int64_t) {});
  });
}

TEST_F(SubscriptionThreadDispatcherTest, DestroyDispatcherAfterClearingCallbacksIsSafe) {
  EXPECT_NO_THROW({
    SubscriptionThreadDispatcher dispatcher;
    (void)dispatcher.trySetOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
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
        (void)dispatcher.trySetOnAudioFrameCallback(id, "mic-main", [](const AudioFrame&) {});
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
        (void)dispatcher.trySetOnAudioFrameCallback(id, "mic-main", [](const AudioFrame&) {});
        (void)dispatcher.trySetOnVideoFrameCallback(id, "cam-main", [](const VideoFrame&, std::int64_t) {});
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
    (void)dispatcher.trySetOnAudioFrameCallback("participant-" + std::to_string(i), "mic-main",
                                                [](const AudioFrame&) {});
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

TEST_F(SubscriptionThreadDispatcherTest, MarkDataReaderFinishedIfCurrentKeepsMatchingEntry) {
  SubscriptionThreadDispatcher dispatcher;
  auto reader = std::make_shared<ActiveDataReader>();
  activeDataReaders(dispatcher)[0] = reader;

  markDataReaderFinishedIfCurrent(dispatcher, 0, reader);

  ASSERT_EQ(activeDataReaders(dispatcher).size(), 1u);
  EXPECT_EQ(activeDataReaders(dispatcher)[0], reader);
  EXPECT_TRUE(reader->finished);
}

TEST_F(SubscriptionThreadDispatcherTest, MarkDataReaderFinishedIfCurrentLeavesReplacedEntry) {
  SubscriptionThreadDispatcher dispatcher;
  auto original = std::make_shared<ActiveDataReader>();
  auto replacement = std::make_shared<ActiveDataReader>();
  activeDataReaders(dispatcher)[0] = replacement;

  // The original reader exited after being replaced; it must not mark the
  // newer reader that now owns the same callback id.
  markDataReaderFinishedIfCurrent(dispatcher, 0, original);

  ASSERT_EQ(activeDataReaders(dispatcher).size(), 1u);
  EXPECT_EQ(activeDataReaders(dispatcher)[0], replacement);
  EXPECT_FALSE(replacement->finished);
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
  (void)dispatcher.trySetOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

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
  (void)dispatcher.trySetOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {});

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
// trySetOn* replacement semantics: registration is rejected while a reader is
// active; clearing first allows re-registration.
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, TrySetOnAudioWhileReaderActiveIsRejected) {
  SubscriptionThreadDispatcher dispatcher;
  ASSERT_TRUE(dispatcher.trySetOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}));

  // Simulate an already-running reader for this subscription.
  const CallbackKey key{"alice", "mic"};
  activeReaders(dispatcher)[key].track_sid = "TR_audio_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  EXPECT_FALSE(dispatcher.trySetOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}))
      << "Replacing a callback while its reader is active must be rejected";
}

TEST_F(SubscriptionThreadDispatcherTest, TrySetOnVideoWhileReaderActiveIsRejected) {
  SubscriptionThreadDispatcher dispatcher;
  ASSERT_TRUE(dispatcher.trySetOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {}));

  const CallbackKey key{"alice", "cam"};
  activeReaders(dispatcher)[key].track_sid = "TR_video_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  EXPECT_FALSE(dispatcher.trySetOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {}));
  EXPECT_FALSE(dispatcher.trySetOnVideoFrameEventCallback("alice", "cam", [](const VideoFrameEvent&) {}));
}

TEST_F(SubscriptionThreadDispatcherTest, ClearThenTrySetOnAudioRegistersNewCallback) {
  SubscriptionThreadDispatcher dispatcher;
  ASSERT_TRUE(dispatcher.trySetOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}));

  const CallbackKey key{"alice", "mic"};
  activeReaders(dispatcher)[key].track_sid = "TR_audio_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  // Rejected while active, accepted once the reader is cleared.
  ASSERT_FALSE(dispatcher.trySetOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}));
  dispatcher.clearOnAudioFrameCallback("alice", "mic");
  EXPECT_EQ(activeReaderCount(dispatcher), 0u);
  EXPECT_TRUE(dispatcher.trySetOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}));
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, ClearThenDeprecatedSetOnAudioRegistersNewCallback) {
  SubscriptionThreadDispatcher dispatcher;
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

  const CallbackKey key{"alice", "mic"};
  activeReaders(dispatcher)[key].track_sid = "TR_audio_1";
  ASSERT_EQ(activeReaderCount(dispatcher), 1u);

  dispatcher.clearOnAudioFrameCallback("alice", "mic");
  EXPECT_EQ(activeReaderCount(dispatcher), 0u);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, TrySetOnAudioWithoutActiveReaderOverwritesRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_TRUE(dispatcher.trySetOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}));
  // No reader is active, so re-registering the same key is allowed and simply
  // overwrites the stored callback.
  EXPECT_TRUE(dispatcher.trySetOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}));
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
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
  (void)dispatcher.trySetOnAudioFrameCallback("alice", "mic-main", [](const AudioFrame&) {});
  (void)dispatcher.trySetOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {});
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

} // namespace livekit
