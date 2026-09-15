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
/// @brief Unit tests for SubscriptionThreadDispatcher registration state and
/// reader-thread lifecycle.
///
/// No LiveKit server is involved. Tracks are `FakeMediaTrack`s with an invalid
/// FFI handle: the dispatcher retains the subscription but stream creation
/// fails inside the FFI layer, so no real reader ever starts. Reader-thread
/// lifecycle (drain, join, re-entrancy, EOS reaping, stopAll coordination) is
/// exercised by injecting test-controlled reader threads into the dispatcher's
/// slots through the friend fixture.

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "livekit/subscription_thread_dispatcher.h"

namespace livekit {

namespace {

using namespace std::chrono_literals;

/// A Track whose FFI handle is invalid. The SID/kind checks in the dispatcher
/// run before any FFI call, and stream creation for it fails and is caught, so
/// slot state stays deterministic.
class FakeMediaTrack : public Track {
public:
  FakeMediaTrack(std::string sid, TrackKind kind)
      : Track(FfiHandle(0), std::move(sid), "track", kind, StreamState::STATE_ACTIVE, false, true) {}
};

std::shared_ptr<Track> audioTrack(const std::string& sid = "TR_audio_1") {
  return std::make_shared<FakeMediaTrack>(sid, TrackKind::KIND_AUDIO);
}
std::shared_ptr<Track> videoTrack(const std::string& sid = "TR_video_1") {
  return std::make_shared<FakeMediaTrack>(sid, TrackKind::KIND_VIDEO);
}

/// Test-controlled gate an injected reader thread blocks on until released.
struct Gate {
  std::mutex m;
  std::condition_variable cv;
  bool open{false};

  void release() {
    {
      const std::scoped_lock<std::mutex> lock(m);
      open = true;
    }
    cv.notify_all();
  }
  void wait() {
    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [this] { return open; });
  }
};

/// Releases a gate on scope exit so a dispatcher destroyed later can join the
/// injected reader. Declare it AFTER the dispatcher it protects.
struct AutoRelease {
  std::shared_ptr<Gate> gate;
  ~AutoRelease() {
    if (gate) {
      gate->release();
    }
  }
};

template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds timeout = 3000ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return predicate();
}

/// Time we give a call that is expected to block before concluding it did.
constexpr auto kBlockCheck = 150ms;

AudioFrame makeAudioFrame() { return AudioFrame::create(48000, 1, 480); }
VideoFrame makeVideoFrame() { return VideoFrame::create(16, 16, VideoBufferType::RGBA); }

} // namespace

class SubscriptionThreadDispatcherTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogLevel::Info); }

  void TearDown() override { livekit::shutdown(); }

  using CallbackKey = SubscriptionThreadDispatcher::CallbackKey;
  using CallbackKeyHash = SubscriptionThreadDispatcher::CallbackKeyHash;
  using DataCallbackKey = SubscriptionThreadDispatcher::DataCallbackKey;
  using DataCallbackKeyHash = SubscriptionThreadDispatcher::DataCallbackKeyHash;
  using ActiveReader = SubscriptionThreadDispatcher::ActiveReader;
  using ActiveDataReader = SubscriptionThreadDispatcher::ActiveDataReader;
  using ReaderExit = SubscriptionThreadDispatcher::ReaderExit;

  static auto& audioCallbacks(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.audio_callbacks_; }
  static auto& videoCallbacks(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.video_callbacks_; }
  static auto& activeReaders(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.active_readers_; }
  static auto& dataCallbacks(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.data_callbacks_; }
  static auto& activeDataReaders(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.active_data_readers_; }
  static auto& remoteDataTracks(SubscriptionThreadDispatcher& dispatcher) { return dispatcher.remote_data_tracks_; }
  static int maxActiveReaders() { return SubscriptionThreadDispatcher::kMaxActiveReaders; }

  // --- slot inspection (all take the dispatcher lock) ---

  template <typename F>
  static auto withSlot(SubscriptionThreadDispatcher& d, const CallbackKey& key, F&& f,
                       decltype(f(std::declval<ActiveReader&>())) missing) {
    const std::scoped_lock<std::mutex> lock(d.lock_);
    auto it = d.active_readers_.find(key);
    if (it == d.active_readers_.end()) {
      return missing;
    }
    return f(it->second);
  }

  static bool hasSlot(SubscriptionThreadDispatcher& d, const CallbackKey& key) {
    const std::scoped_lock<std::mutex> lock(d.lock_);
    return d.active_readers_.find(key) != d.active_readers_.end();
  }
  static bool hasRetainedTrack(SubscriptionThreadDispatcher& d, const CallbackKey& key) {
    return withSlot(d, key, [](ActiveReader& s) { return s.track != nullptr; }, false);
  }
  static std::shared_ptr<Track> retainedTrack(SubscriptionThreadDispatcher& d, const CallbackKey& key) {
    return withSlot(d, key, [](ActiveReader& s) { return s.track; }, std::shared_ptr<Track>{});
  }
  static bool hasReaderThread(SubscriptionThreadDispatcher& d, const CallbackKey& key) {
    return withSlot(d, key, [](ActiveReader& s) { return s.thread.joinable(); }, false);
  }
  static bool isDraining(SubscriptionThreadDispatcher& d, const CallbackKey& key) {
    return withSlot(d, key, [](ActiveReader& s) { return s.draining; }, false);
  }
  static bool trackEnded(SubscriptionThreadDispatcher& d, const CallbackKey& key) {
    return withSlot(d, key, [](ActiveReader& s) { return s.track_ended; }, false);
  }
  static std::size_t detachedCount(SubscriptionThreadDispatcher& d, const CallbackKey& key) {
    return withSlot(d, key, [](ActiveReader& s) { return s.detached.size(); }, std::size_t{0});
  }
  static int runningReaderCount(SubscriptionThreadDispatcher& d) {
    const std::scoped_lock<std::mutex> lock(d.lock_);
    return d.runningReaderCountLocked();
  }
  static std::size_t slotCount(SubscriptionThreadDispatcher& d) {
    const std::scoped_lock<std::mutex> lock(d.lock_);
    return d.active_readers_.size();
  }

  /// Inject a running reader into the slot for @p key, exactly as a started
  /// reader would look: retained track, SID, thread, thread id and exit signal.
  /// The thread blocks on @p gate (if given), runs @p body (which may call back
  /// into the dispatcher, like a frame callback would), then signals exit.
  static std::shared_ptr<ReaderExit> injectReader(SubscriptionThreadDispatcher& d, const CallbackKey& key,
                                                  const std::shared_ptr<Track>& track,
                                                  std::shared_ptr<Gate> gate = nullptr,
                                                  std::function<void()> body = {}) {
    auto exit = std::make_shared<ReaderExit>();
    const std::scoped_lock<std::mutex> lock(d.lock_);
    auto& slot = d.active_readers_[key];
    slot.track = track;
    slot.track_sid = track->sid();
    slot.track_ended = false;
    slot.exit = exit;
    slot.thread = std::thread([gate, body, exit]() {
      if (gate) {
        gate->wait();
      }
      if (body) {
        body();
      }
      exit->signal();
    });
    slot.thread_id = slot.thread.get_id();
    return exit;
  }

  /// Inject a running data reader under callback @p id. Same shape as above.
  static std::shared_ptr<ActiveDataReader> injectDataReader(SubscriptionThreadDispatcher& d, DataFrameCallbackId id,
                                                            std::shared_ptr<Gate> gate = nullptr,
                                                            std::function<void()> body = {}) {
    auto reader = std::make_shared<ActiveDataReader>();
    reader->exit = std::make_shared<ReaderExit>();
    auto exit = reader->exit;
    reader->thread = std::thread([gate, body, exit]() {
      if (gate) {
        gate->wait();
      }
      if (body) {
        body();
      }
      exit->signal();
    });
    reader->thread_id = reader->thread.get_id();
    const std::scoped_lock<std::mutex> lock(d.lock_);
    d.active_data_readers_[id] = reader;
    return reader;
  }

  static bool dataReaderDetached(SubscriptionThreadDispatcher& d, DataFrameCallbackId id) {
    const std::scoped_lock<std::mutex> lock(d.lock_);
    auto it = d.active_data_readers_.find(id);
    return it != d.active_data_readers_.end() && it->second && it->second->detached;
  }
  static bool hasDataReader(SubscriptionThreadDispatcher& d, DataFrameCallbackId id) {
    const std::scoped_lock<std::mutex> lock(d.lock_);
    return d.active_data_readers_.find(id) != d.active_data_readers_.end();
  }
  static std::size_t dataReaderCount(SubscriptionThreadDispatcher& d) {
    const std::scoped_lock<std::mutex> lock(d.lock_);
    return d.active_data_readers_.size();
  }
};

// ============================================================================
// ABI: the exported class layout must not change (see header ABI note).
// The value is platform specific; pin it where the toolchain is known.
// ============================================================================

#if defined(__APPLE__) && defined(_LIBCPP_VERSION)
static_assert(sizeof(SubscriptionThreadDispatcher) == 312,
              "SubscriptionThreadDispatcher layout changed: this is an ABI break for consumers of liblivekit");
TEST_F(SubscriptionThreadDispatcherTest, ClassLayoutIsUnchanged) {
  EXPECT_EQ(sizeof(SubscriptionThreadDispatcher), 312u);
}
#endif

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

TEST_F(SubscriptionThreadDispatcherTest, OverwriteAudioCallbackKeepsSingleEntry) {
  SubscriptionThreadDispatcher dispatcher;
  std::atomic<int> counter1{0};
  std::atomic<int> counter2{0};

  dispatcher.setOnAudioFrameCallback("alice", "mic-main", [&counter1](const AudioFrame&) { counter1++; });
  dispatcher.setOnAudioFrameCallback("alice", "mic-main", [&counter2](const AudioFrame&) { counter2++; });

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u) << "Re-registering with the same key should overwrite, not add";
}

TEST_F(SubscriptionThreadDispatcherTest, OverwriteVideoCallbackKeepsSingleEntry) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.setOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {});
  dispatcher.setOnVideoFrameCallback("alice", "cam-main", [](const VideoFrame&, std::int64_t) {});

  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
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
// Retained subscriptions (GitHub issue #235)
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, HandleTrackSubscribedRetainsTrackWithoutRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  const CallbackKey key{"alice", "mic"};
  auto track = audioTrack();

  dispatcher.handleTrackSubscribed("alice", "mic", track);

  EXPECT_TRUE(hasSlot(dispatcher, key));
  EXPECT_EQ(retainedTrack(dispatcher, key), track);
  EXPECT_FALSE(hasReaderThread(dispatcher, key)) << "no callback registered, so no reader may start";
  EXPECT_FALSE(isDraining(dispatcher, key));
  EXPECT_EQ(slotCount(dispatcher), 1u);
}

TEST_F(SubscriptionThreadDispatcherTest, HandleTrackSubscribedWithNullTrackIsIgnored) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_NO_THROW(dispatcher.handleTrackSubscribed("alice", "mic", nullptr));
  EXPECT_EQ(slotCount(dispatcher), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, HandleTrackUnsubscribedReleasesRetainedSlot) {
  SubscriptionThreadDispatcher dispatcher;
  const CallbackKey key{"alice", "mic"};
  dispatcher.handleTrackSubscribed("alice", "mic", audioTrack());
  ASSERT_TRUE(hasSlot(dispatcher, key));

  dispatcher.handleTrackUnsubscribed("alice", TrackSource::SOURCE_MICROPHONE, "mic");
  EXPECT_FALSE(hasSlot(dispatcher, key));
}

TEST_F(SubscriptionThreadDispatcherTest, HandleTrackUnsubscribedForUnknownKeyIsNoOp) {
  SubscriptionThreadDispatcher dispatcher;
  EXPECT_NO_THROW(dispatcher.handleTrackUnsubscribed("nobody", TrackSource::SOURCE_UNKNOWN, "missing"));
  EXPECT_EQ(slotCount(dispatcher), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioAfterTrackSubscribedKeepsSlotAndRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  const CallbackKey key{"alice", "mic"};
  dispatcher.handleTrackSubscribed("alice", "mic", audioTrack());

  // Late registration (#235): the dispatcher tries to start a reader right
  // away. With a fake track the stream cannot be created, which must leave the
  // slot idle and retained rather than dropping it or the registration.
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
  EXPECT_TRUE(hasRetainedTrack(dispatcher, key));
  EXPECT_FALSE(isDraining(dispatcher, key));
  EXPECT_FALSE(trackEnded(dispatcher, key));
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnVideoAndVideoEventAfterTrackSubscribedKeepSlotAndRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  const CallbackKey key{"alice", "cam"};
  dispatcher.handleTrackSubscribed("alice", "cam", videoTrack());

  dispatcher.setOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {});
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
  EXPECT_TRUE(hasRetainedTrack(dispatcher, key));

  dispatcher.setOnVideoFrameEventCallback("alice", "cam", [](const VideoFrameEvent&) {});
  EXPECT_EQ(videoCallbacks(dispatcher).size(), 1u);
  EXPECT_TRUE(hasRetainedTrack(dispatcher, key));
  EXPECT_FALSE(isDraining(dispatcher, key));
}

TEST_F(SubscriptionThreadDispatcherTest, RegistrationBeforeSubscribeSurvivesSubscribeAndUnsubscribe) {
  SubscriptionThreadDispatcher dispatcher;
  const CallbackKey key{"alice", "mic"};
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  EXPECT_FALSE(hasSlot(dispatcher, key)) << "registration alone must not create a subscription slot";

  dispatcher.handleTrackSubscribed("alice", "mic", audioTrack());
  EXPECT_TRUE(hasRetainedTrack(dispatcher, key));

  dispatcher.handleTrackUnsubscribed("alice", TrackSource::SOURCE_MICROPHONE, "mic");
  EXPECT_FALSE(hasSlot(dispatcher, key));
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u) << "unsubscribe keeps the registration for re-subscription";
}

TEST_F(SubscriptionThreadDispatcherTest, ClearOnAudioKeepsRetainedSubscription) {
  SubscriptionThreadDispatcher dispatcher;
  const CallbackKey key{"alice", "mic"};
  dispatcher.handleTrackSubscribed("alice", "mic", audioTrack());
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});

  dispatcher.clearOnAudioFrameCallback("alice", "mic");

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 0u);
  EXPECT_TRUE(hasRetainedTrack(dispatcher, key)) << "the track is still subscribed; a later registration must work";
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioForVideoTrackDoesNotDisturbSlot) {
  SubscriptionThreadDispatcher dispatcher;
  const CallbackKey key{"alice", "cam"};
  auto track = videoTrack();
  dispatcher.handleTrackSubscribed("alice", "cam", track);

  dispatcher.setOnAudioFrameCallback("alice", "cam", [](const AudioFrame&) {});

  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
  EXPECT_EQ(retainedTrack(dispatcher, key), track);
  EXPECT_FALSE(hasReaderThread(dispatcher, key));
}

TEST_F(SubscriptionThreadDispatcherTest, DuplicateSubscribeKeepsSingleSlotAndNewestTrack) {
  SubscriptionThreadDispatcher dispatcher;
  const CallbackKey key{"alice", "mic"};
  auto first = audioTrack("TR_audio_1");
  auto second = audioTrack("TR_audio_2");

  dispatcher.handleTrackSubscribed("alice", "mic", first);
  dispatcher.handleTrackSubscribed("alice", "mic", second);

  EXPECT_EQ(slotCount(dispatcher), 1u);
  EXPECT_EQ(retainedTrack(dispatcher, key), second);
}

TEST_F(SubscriptionThreadDispatcherTest, StopAllReleasesRetainedSubscriptionsAndRegistrations) {
  SubscriptionThreadDispatcher dispatcher;
  dispatcher.handleTrackSubscribed("alice", "mic", audioTrack("TR_a"));
  dispatcher.handleTrackSubscribed("bob", "cam", videoTrack("TR_b"));
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  ASSERT_EQ(slotCount(dispatcher), 2u);

  dispatcher.stopAll();

  EXPECT_EQ(slotCount(dispatcher), 0u);
  EXPECT_TRUE(audioCallbacks(dispatcher).empty());
  EXPECT_TRUE(videoCallbacks(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, DestroyDispatcherWithRetainedSubscriptionsIsSafe) {
  EXPECT_NO_THROW({
    SubscriptionThreadDispatcher dispatcher;
    dispatcher.handleTrackSubscribed("alice", "mic", audioTrack());
    dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  });
}

// ============================================================================
// Reader replacement: the previous reader is stopped and joined before the
// setter returns, and the new registration is what remains.
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioWhileReaderActiveStopsAndJoinsPreviousReader) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const AutoRelease guard{gate};
  const CallbackKey key{"alice", "mic"};
  std::atomic<int> old_calls{0};
  std::atomic<int> new_calls{0};
  dispatcher.setOnAudioFrameCallback("alice", "mic", [&](const AudioFrame&) { old_calls++; });
  // Pretend the subscription arrived and a reader is serving the old callback.
  injectReader(dispatcher, key, audioTrack(), gate);

  // Replace while the reader is running: the setter must join it first, so it
  // blocks until the gate opens.
  auto setter = std::async(std::launch::async, [&] {
    dispatcher.setOnAudioFrameCallback("alice", "mic", [&](const AudioFrame&) { new_calls++; });
  });

  EXPECT_EQ(setter.wait_for(kBlockCheck), std::future_status::timeout) << "setter must block on the join";
  EXPECT_TRUE(isDraining(dispatcher, key));
  EXPECT_FALSE(hasReaderThread(dispatcher, key)) << "the thread was moved out to the drain owner";

  gate->release();
  setter.get();

  EXPECT_FALSE(isDraining(dispatcher, key));
  EXPECT_FALSE(hasReaderThread(dispatcher, key)) << "fake track: the replacement reader cannot start";
  EXPECT_TRUE(hasRetainedTrack(dispatcher, key));
  ASSERT_EQ(audioCallbacks(dispatcher).size(), 1u);
  const auto frame = makeAudioFrame();
  audioCallbacks(dispatcher).at(key).callback(frame);
  EXPECT_EQ(new_calls.load(), 1) << "the stored registration must be the newest callback";
  EXPECT_EQ(old_calls.load(), 0);
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnVideoWhileReaderActiveStopsAndJoinsPreviousReader) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const AutoRelease guard{gate};
  const CallbackKey key{"alice", "cam"};
  injectReader(dispatcher, key, videoTrack(), gate);
  std::atomic<int> calls{0};

  auto setter = std::async(std::launch::async, [&] {
    dispatcher.setOnVideoFrameCallback("alice", "cam", [&](const VideoFrame&, std::int64_t) { calls++; });
  });
  EXPECT_EQ(setter.wait_for(kBlockCheck), std::future_status::timeout);
  EXPECT_TRUE(isDraining(dispatcher, key));

  gate->release();
  setter.get();

  EXPECT_FALSE(isDraining(dispatcher, key));
  EXPECT_TRUE(hasRetainedTrack(dispatcher, key));
  ASSERT_EQ(videoCallbacks(dispatcher).size(), 1u);
  const auto frame = makeVideoFrame();
  videoCallbacks(dispatcher).at(key).legacy_callback(frame, 0);
  EXPECT_EQ(calls.load(), 1);
  EXPECT_FALSE(static_cast<bool>(videoCallbacks(dispatcher).at(key).event_callback));
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnVideoEventWhileReaderActiveStopsReaderAndDisplacesLegacyCallback) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const AutoRelease guard{gate};
  const CallbackKey key{"alice", "cam"};
  dispatcher.setOnVideoFrameCallback("alice", "cam", [](const VideoFrame&, std::int64_t) {});
  injectReader(dispatcher, key, videoTrack(), gate);

  auto setter = std::async(std::launch::async, [&] {
    dispatcher.setOnVideoFrameEventCallback("alice", "cam", [](const VideoFrameEvent&) {});
  });
  EXPECT_EQ(setter.wait_for(kBlockCheck), std::future_status::timeout);
  gate->release();
  setter.get();

  ASSERT_EQ(videoCallbacks(dispatcher).size(), 1u);
  EXPECT_TRUE(static_cast<bool>(videoCallbacks(dispatcher).at(key).event_callback));
  EXPECT_FALSE(static_cast<bool>(videoCallbacks(dispatcher).at(key).legacy_callback))
      << "the two video registrations share one slot";
  EXPECT_FALSE(isDraining(dispatcher, key));
}

TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioDoesNotStopVideoReaderForSameKey) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const AutoRelease guard{gate};
  const CallbackKey key{"alice", "cam"};
  injectReader(dispatcher, key, videoTrack(), gate);

  dispatcher.setOnAudioFrameCallback("alice", "cam", [](const AudioFrame&) {}); // returns without joining

  EXPECT_TRUE(hasReaderThread(dispatcher, key)) << "a reader of another kind must be left alone";
  EXPECT_FALSE(isDraining(dispatcher, key));
  gate->release();
}

TEST_F(SubscriptionThreadDispatcherTest, ReplacementLeavesOtherKeysReadersUntouched) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate_a = std::make_shared<Gate>();
  auto gate_b = std::make_shared<Gate>();
  const AutoRelease guard_a{gate_a};
  const AutoRelease guard_b{gate_b};
  const CallbackKey alice{"alice", "mic"};
  const CallbackKey bob{"bob", "mic"};
  injectReader(dispatcher, alice, audioTrack("TR_a"), gate_a);
  injectReader(dispatcher, bob, audioTrack("TR_b"), gate_b);

  auto setter = std::async(std::launch::async,
                           [&] { dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}); });
  EXPECT_EQ(setter.wait_for(kBlockCheck), std::future_status::timeout);
  EXPECT_TRUE(hasReaderThread(dispatcher, bob));
  EXPECT_FALSE(isDraining(dispatcher, bob));

  gate_a->release();
  setter.get();
  EXPECT_TRUE(hasReaderThread(dispatcher, bob));
  gate_b->release();
}

TEST_F(SubscriptionThreadDispatcherTest, ClearOnAudioWhileReaderActiveJoinsReaderAndKeepsSubscription) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const AutoRelease guard{gate};
  const CallbackKey key{"alice", "mic"};
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  injectReader(dispatcher, key, audioTrack(), gate);

  auto clearer = std::async(std::launch::async, [&] { dispatcher.clearOnAudioFrameCallback("alice", "mic"); });
  EXPECT_EQ(clearer.wait_for(kBlockCheck), std::future_status::timeout) << "clear must block on the join";
  EXPECT_TRUE(isDraining(dispatcher, key));

  gate->release();
  clearer.get();

  EXPECT_TRUE(audioCallbacks(dispatcher).empty());
  EXPECT_FALSE(hasReaderThread(dispatcher, key));
  EXPECT_FALSE(isDraining(dispatcher, key));
  EXPECT_TRUE(hasRetainedTrack(dispatcher, key));
}

TEST_F(SubscriptionThreadDispatcherTest, HandleTrackUnsubscribedWhileReaderActiveJoinsAndReleasesSlot) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const AutoRelease guard{gate};
  const CallbackKey key{"alice", "mic"};
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  injectReader(dispatcher, key, audioTrack(), gate);

  auto unsub = std::async(std::launch::async,
                          [&] { dispatcher.handleTrackUnsubscribed("alice", TrackSource::SOURCE_MICROPHONE, "mic"); });
  EXPECT_EQ(unsub.wait_for(kBlockCheck), std::future_status::timeout);
  EXPECT_TRUE(isDraining(dispatcher, key));
  EXPECT_FALSE(hasRetainedTrack(dispatcher, key)) << "the track is forgotten immediately";

  gate->release();
  unsub.get();

  EXPECT_FALSE(hasSlot(dispatcher, key)) << "nothing remains once the reader is joined";
  EXPECT_EQ(audioCallbacks(dispatcher).size(), 1u);
}

// ============================================================================
// Duplicate and replacement subscription events
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, HandleTrackSubscribedWithSameSidWhileRunningIsNoOp) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const AutoRelease guard{gate};
  const CallbackKey key{"alice", "mic"};
  injectReader(dispatcher, key, audioTrack("TR_same"), gate);

  dispatcher.handleTrackSubscribed("alice", "mic", audioTrack("TR_same")); // must not block or drain

  EXPECT_TRUE(hasReaderThread(dispatcher, key));
  EXPECT_FALSE(isDraining(dispatcher, key));
  gate->release();
}

TEST_F(SubscriptionThreadDispatcherTest, HandleTrackSubscribedWithNewSidWhileRunningReplacesReader) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const AutoRelease guard{gate};
  const CallbackKey key{"alice", "mic"};
  injectReader(dispatcher, key, audioTrack("TR_old"), gate);
  auto republished = audioTrack("TR_new");

  auto resub = std::async(std::launch::async, [&] { dispatcher.handleTrackSubscribed("alice", "mic", republished); });
  EXPECT_EQ(resub.wait_for(kBlockCheck), std::future_status::timeout) << "republish joins the previous reader";
  EXPECT_TRUE(isDraining(dispatcher, key));
  EXPECT_EQ(retainedTrack(dispatcher, key), republished);

  gate->release();
  resub.get();

  EXPECT_FALSE(isDraining(dispatcher, key));
  EXPECT_EQ(retainedTrack(dispatcher, key), republished);
}

// ============================================================================
// Drain protocol under concurrency
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, ConcurrentSetterDuringDrainWaitsForPreviousReaderExit) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const AutoRelease guard{gate};
  const CallbackKey key{"alice", "mic"};
  injectReader(dispatcher, key, audioTrack(), gate);
  std::atomic<int> first_calls{0};
  std::atomic<int> second_calls{0};

  auto first = std::async(std::launch::async, [&] {
    dispatcher.setOnAudioFrameCallback("alice", "mic", [&](const AudioFrame&) { first_calls++; });
  });
  ASSERT_TRUE(waitFor([&] { return isDraining(dispatcher, key); }));

  auto second = std::async(std::launch::async, [&] {
    dispatcher.setOnAudioFrameCallback("alice", "mic", [&](const AudioFrame&) { second_calls++; });
  });
  EXPECT_EQ(second.wait_for(kBlockCheck), std::future_status::timeout)
      << "a concurrent setter must wait for the reader being drained by the first caller";
  EXPECT_EQ(first.wait_for(0ms), std::future_status::timeout);

  gate->release();
  first.get();
  second.get();

  EXPECT_FALSE(isDraining(dispatcher, key));
  const auto frame = makeAudioFrame();
  audioCallbacks(dispatcher).at(key).callback(frame);
  EXPECT_EQ(second_calls.load(), 1) << "the newest registration wins";
  EXPECT_EQ(first_calls.load(), 0);
}

TEST_F(SubscriptionThreadDispatcherTest, StopAllWaitsForReaderBeingDrainedByAnotherCaller) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const AutoRelease guard{gate};
  const CallbackKey key{"alice", "mic"};
  injectReader(dispatcher, key, audioTrack(), gate);

  auto setter = std::async(std::launch::async,
                           [&] { dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}); });
  ASSERT_TRUE(waitFor([&] { return isDraining(dispatcher, key); }));

  auto stopper = std::async(std::launch::async, [&] { dispatcher.stopAll(); });
  EXPECT_EQ(stopper.wait_for(kBlockCheck), std::future_status::timeout)
      << "stopAll must not return while a reader drained elsewhere is still running";
  EXPECT_EQ(slotCount(dispatcher), 0u) << "state is cleared immediately; only the wait remains";

  gate->release();
  stopper.get();
  setter.get();
  EXPECT_EQ(slotCount(dispatcher), 0u) << "the drain owner must not resurrect the slot";
}

TEST_F(SubscriptionThreadDispatcherTest, DrainOwnerLeavesSlotRecreatedAfterStopAllUntouched) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const AutoRelease guard{gate};
  const CallbackKey key{"alice", "mic"};
  injectReader(dispatcher, key, audioTrack("TR_old"), gate);

  auto setter = std::async(std::launch::async,
                           [&] { dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {}); });
  ASSERT_TRUE(waitFor([&] { return isDraining(dispatcher, key); }));

  auto stopper = std::async(std::launch::async, [&] { dispatcher.stopAll(); });
  ASSERT_TRUE(waitFor([&] { return slotCount(dispatcher) == 0; }));

  // A fresh subscription arrives while the old drain is still in flight.
  auto fresh = audioTrack("TR_new");
  dispatcher.handleTrackSubscribed("alice", "mic", fresh);
  ASSERT_TRUE(hasSlot(dispatcher, key));

  gate->release();
  stopper.get();
  setter.get();

  EXPECT_TRUE(hasSlot(dispatcher, key)) << "the old drain owner must not touch the recreated slot";
  EXPECT_EQ(retainedTrack(dispatcher, key), fresh);
  EXPECT_FALSE(isDraining(dispatcher, key));
}

// ============================================================================
// Re-entrant lifecycle calls from inside the reader's own callback
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, SetOnAudioFromInsideItsOwnReaderDetachesAndReplaces) {
  SubscriptionThreadDispatcher dispatcher;
  const CallbackKey key{"alice", "mic"};
  std::atomic<int> new_calls{0};
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  auto exit = injectReader(dispatcher, key, audioTrack(), nullptr, [&] {
    // What a frame callback that re-registers itself would do.
    dispatcher.setOnAudioFrameCallback("alice", "mic", [&](const AudioFrame&) { new_calls++; });
  });

  ASSERT_TRUE(waitFor([&] { return exit->isDone(); })) << "the reader must not deadlock on itself";
  ASSERT_TRUE(waitFor([&] { return !hasReaderThread(dispatcher, key); }));

  EXPECT_FALSE(isDraining(dispatcher, key));
  EXPECT_EQ(detachedCount(dispatcher, key), 1u) << "the detached reader is remembered until reaped";
  const auto frame = makeAudioFrame();
  audioCallbacks(dispatcher).at(key).callback(frame);
  EXPECT_EQ(new_calls.load(), 1);

  dispatcher.setOnVideoFrameCallback("bob", "cam", [](const VideoFrame&, std::int64_t) {}); // any call reaps
  EXPECT_EQ(detachedCount(dispatcher, key), 0u);
  EXPECT_NO_THROW(dispatcher.stopAll());
}

TEST_F(SubscriptionThreadDispatcherTest, ClearOnAudioFromInsideItsOwnReaderDetaches) {
  SubscriptionThreadDispatcher dispatcher;
  const CallbackKey key{"alice", "mic"};
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  auto exit = injectReader(dispatcher, key, audioTrack(), nullptr,
                           [&] { dispatcher.clearOnAudioFrameCallback("alice", "mic"); });

  ASSERT_TRUE(waitFor([&] { return exit->isDone(); }));
  EXPECT_TRUE(audioCallbacks(dispatcher).empty());
  EXPECT_FALSE(hasReaderThread(dispatcher, key));
  EXPECT_TRUE(hasRetainedTrack(dispatcher, key));
  EXPECT_NO_THROW(dispatcher.stopAll());
}

TEST_F(SubscriptionThreadDispatcherTest, StopAllFromInsideReaderDetachesInsteadOfSelfJoining) {
  SubscriptionThreadDispatcher dispatcher;
  const CallbackKey key{"alice", "mic"};
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  auto exit = injectReader(dispatcher, key, audioTrack(), nullptr, [&] { dispatcher.stopAll(); });

  ASSERT_TRUE(waitFor([&] { return exit->isDone(); })) << "stopAll from a reader must not terminate or deadlock";
  EXPECT_EQ(slotCount(dispatcher), 0u);
  EXPECT_TRUE(audioCallbacks(dispatcher).empty());
}

TEST_F(SubscriptionThreadDispatcherTest, StopAllWaitsForPreviouslyDetachedReader) {
  SubscriptionThreadDispatcher dispatcher;
  const CallbackKey key{"alice", "mic"};
  auto after_clear = std::make_shared<Gate>();
  const AutoRelease guard{after_clear};
  std::atomic<bool> cleared{false};
  auto exit = injectReader(dispatcher, key, audioTrack(), nullptr, [&] {
    dispatcher.clearOnAudioFrameCallback("alice", "mic"); // detaches this very reader
    cleared = true;
    after_clear->wait(); // ...but the "callback" keeps running for a while
  });
  ASSERT_TRUE(waitFor([&] { return cleared.load(); }));
  ASSERT_EQ(detachedCount(dispatcher, key), 1u);

  auto stopper = std::async(std::launch::async, [&] { dispatcher.stopAll(); });
  EXPECT_EQ(stopper.wait_for(kBlockCheck), std::future_status::timeout)
      << "stopAll must wait for a detached reader that is still inside its callback";

  after_clear->release();
  stopper.get();
  EXPECT_TRUE(exit->isDone());
}

// ============================================================================
// End of stream: a reader that exits on its own is reaped and not restarted
// until a new subscribe event or an explicit re-registration.
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, FinishedReaderIsReapedByNextLifecycleCall) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const CallbackKey key{"alice", "mic"};
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  auto exit = injectReader(dispatcher, key, audioTrack(), gate);

  gate->release(); // the "stream" ends on its own
  ASSERT_TRUE(waitFor([&] { return exit->isDone(); }));
  EXPECT_TRUE(hasReaderThread(dispatcher, key)) << "finished but not yet reaped";
  EXPECT_EQ(runningReaderCount(dispatcher), 0) << "a finished reader no longer counts against the cap";

  dispatcher.setOnVideoFrameCallback("bob", "cam", [](const VideoFrame&, std::int64_t) {}); // unrelated call

  EXPECT_FALSE(hasReaderThread(dispatcher, key)) << "joined by the reap";
  EXPECT_FALSE(isDraining(dispatcher, key));
  EXPECT_TRUE(hasRetainedTrack(dispatcher, key)) << "the subscription is kept until unsubscribe";
  EXPECT_TRUE(trackEnded(dispatcher, key)) << "auto-restart is suppressed for a track whose stream ended";
}

TEST_F(SubscriptionThreadDispatcherTest, TrackEndedIsClearedByResubscribe) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const CallbackKey key{"alice", "mic"};
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  auto exit = injectReader(dispatcher, key, audioTrack("TR_1"), gate);
  gate->release();
  ASSERT_TRUE(waitFor([&] { return exit->isDone(); }));
  dispatcher.handleTrackUnsubscribed("bob", TrackSource::SOURCE_UNKNOWN, "other"); // reap
  ASSERT_TRUE(trackEnded(dispatcher, key));

  dispatcher.handleTrackSubscribed("alice", "mic", audioTrack("TR_1"));
  EXPECT_FALSE(trackEnded(dispatcher, key));
}

TEST_F(SubscriptionThreadDispatcherTest, TrackEndedIsClearedByReRegistration) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const CallbackKey key{"alice", "mic"};
  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  auto exit = injectReader(dispatcher, key, audioTrack(), gate);
  gate->release();
  ASSERT_TRUE(waitFor([&] { return exit->isDone(); }));
  dispatcher.handleTrackUnsubscribed("bob", TrackSource::SOURCE_UNKNOWN, "other"); // reap
  ASSERT_TRUE(trackEnded(dispatcher, key));

  dispatcher.setOnAudioFrameCallback("alice", "mic", [](const AudioFrame&) {});
  EXPECT_FALSE(trackEnded(dispatcher, key));
}

TEST_F(SubscriptionThreadDispatcherTest, FinishedReaderWithSameSidIsReplacedOnResubscribe) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const CallbackKey key{"alice", "mic"};
  auto exit = injectReader(dispatcher, key, audioTrack("TR_same"), gate);
  gate->release();
  ASSERT_TRUE(waitFor([&] { return exit->isDone(); }));

  // Same SID, but the previous reader is finished: it must be reaped rather
  // than deduplicated as "already active".
  dispatcher.handleTrackSubscribed("alice", "mic", audioTrack("TR_same"));

  EXPECT_FALSE(hasReaderThread(dispatcher, key));
  EXPECT_FALSE(isDraining(dispatcher, key));
  EXPECT_FALSE(trackEnded(dispatcher, key));
  EXPECT_TRUE(hasRetainedTrack(dispatcher, key));
}

TEST_F(SubscriptionThreadDispatcherTest, RunningReaderCountExcludesFinishedAndDrainingReaders) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate_a = std::make_shared<Gate>();
  auto gate_b = std::make_shared<Gate>();
  const AutoRelease guard_a{gate_a};
  const AutoRelease guard_b{gate_b};
  const CallbackKey alice{"alice", "mic"};
  const CallbackKey bob{"bob", "mic"};
  auto exit_a = injectReader(dispatcher, alice, audioTrack("TR_a"), gate_a);
  injectReader(dispatcher, bob, audioTrack("TR_b"), gate_b);
  EXPECT_EQ(runningReaderCount(dispatcher), 2);

  gate_a->release();
  ASSERT_TRUE(waitFor([&] { return exit_a->isDone(); }));
  EXPECT_EQ(runningReaderCount(dispatcher), 1);

  auto setter = std::async(std::launch::async,
                           [&] { dispatcher.setOnAudioFrameCallback("bob", "mic", [](const AudioFrame&) {}); });
  ASSERT_TRUE(waitFor([&] { return isDraining(dispatcher, bob); }));
  EXPECT_EQ(runningReaderCount(dispatcher), 0) << "a reader mid-drain is not counted";

  gate_b->release();
  setter.get();
}

// ============================================================================
// Data readers: removal joins, self-removal detaches, finished entries reaped
// ============================================================================

TEST_F(SubscriptionThreadDispatcherTest, RemoveDataCallbackJoinsItsReader) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const AutoRelease guard{gate};
  auto id = dispatcher.addOnDataFrameCallback("alice", "track",
                                              [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
  injectDataReader(dispatcher, id, gate);
  EXPECT_EQ(runningReaderCount(dispatcher), 1) << "data readers count against the shared cap";

  auto remover = std::async(std::launch::async, [&] { dispatcher.removeOnDataFrameCallback(id); });
  EXPECT_EQ(remover.wait_for(kBlockCheck), std::future_status::timeout) << "removal must block on the join";
  EXPECT_FALSE(hasDataReader(dispatcher, id)) << "the entry is extracted immediately";

  gate->release();
  remover.get();
  EXPECT_TRUE(dataCallbacks(dispatcher).empty());
  EXPECT_EQ(dataReaderCount(dispatcher), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, RemoveDataCallbackFromInsideItsOwnReaderDetachesAndKeepsEntryUntilReaped) {
  SubscriptionThreadDispatcher dispatcher;
  auto id = dispatcher.addOnDataFrameCallback("alice", "track",
                                              [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
  auto after_remove = std::make_shared<Gate>();
  const AutoRelease guard{after_remove};
  std::atomic<bool> removed{false};
  auto reader = injectDataReader(dispatcher, id, nullptr, [&] {
    dispatcher.removeOnDataFrameCallback(id); // from inside "its own callback"
    removed = true;
    after_remove->wait();
  });

  ASSERT_TRUE(waitFor([&] { return removed.load(); })) << "must not self-join";
  EXPECT_TRUE(dataCallbacks(dispatcher).empty());
  EXPECT_TRUE(dataReaderDetached(dispatcher, id)) << "kept so stopAll can still wait for it";

  auto stopper = std::async(std::launch::async, [&] { dispatcher.stopAll(); });
  EXPECT_EQ(stopper.wait_for(kBlockCheck), std::future_status::timeout);

  after_remove->release();
  stopper.get();
  EXPECT_TRUE(reader->exit->isDone());
  EXPECT_EQ(dataReaderCount(dispatcher), 0u);
}

TEST_F(SubscriptionThreadDispatcherTest, FinishedDataReaderIsErasedByNextLifecycleCall) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  auto id = dispatcher.addOnDataFrameCallback("alice", "track",
                                              [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
  auto reader = injectDataReader(dispatcher, id, gate);
  gate->release();
  ASSERT_TRUE(waitFor([&] { return reader->exit->isDone(); }));
  EXPECT_TRUE(hasDataReader(dispatcher, id));
  EXPECT_EQ(runningReaderCount(dispatcher), 0);

  dispatcher.handleDataTrackUnpublished("some-other-sid"); // any lifecycle call reaps

  EXPECT_FALSE(hasDataReader(dispatcher, id));
  EXPECT_EQ(dataCallbacks(dispatcher).size(), 1u) << "the registration survives for a republish";
}

TEST_F(SubscriptionThreadDispatcherTest, StopAllJoinsInjectedDataReader) {
  SubscriptionThreadDispatcher dispatcher;
  auto gate = std::make_shared<Gate>();
  const AutoRelease guard{gate};
  auto id = dispatcher.addOnDataFrameCallback("alice", "track",
                                              [](const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {});
  auto reader = injectDataReader(dispatcher, id, gate);

  auto stopper = std::async(std::launch::async, [&] { dispatcher.stopAll(); });
  EXPECT_EQ(stopper.wait_for(kBlockCheck), std::future_status::timeout);
  EXPECT_TRUE(reader->cancelled.load()) << "cancelled is set before the stream is closed";

  gate->release();
  stopper.get();
  EXPECT_EQ(dataReaderCount(dispatcher), 0u);
}

} // namespace livekit
