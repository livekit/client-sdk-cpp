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

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>

#include "../common/ffi_utils.h"
#include "ffi.pb.h"
#include "ffi_client.h"

namespace livekit::test {

namespace {

volatile bool g_sigterm_received = false;

// Waits for listener entry or drain completion should finish in milliseconds
// This is a generous anti-hang bound for CI thread scheduling, not expected latency
constexpr auto kListenerSyncTimeout = std::chrono::seconds(5);

// Has to be registered globally per csignal API
void handleSignal(int signal) {
  if (signal == SIGTERM) {
    g_sigterm_received = true;
  }
}

// Simple helper to emit a test event
void emitEvent() {
  proto::FfiEvent event;
  auto* record = event.mutable_logs()->add_records();
  record->set_level(proto::LOG_INFO);
  record->set_target("test");
  record->set_message("listener event");

  emitFfiEvent(event);
}

// Minimal stand-in for Room that mirrors its relationship to FfiClient:
//   - it registers an FFI listener whose callback dereferences `this`
//     (like Room's `[this](const FfiEvent& e){ onEvent(e); }`), and
//   - it tears that listener down in its destructor
//     (like ~Room -> disconnect -> removeListener).
//
// This is the object the user's bug report is about: if the FFI thread is
// dispatching an event into the listener while the object is destroyed,
// the callback must never touch freed memory. `magic_` is a liveness
// sentinel so a use-after-free is observable even without a sanitizer.
class FakeRoom {
public:
  static constexpr std::uint32_t kAlive = 0xA11ECAFEU;
  static constexpr std::uint32_t kDead = 0xDEADBEEFU;

  FakeRoom() {
    listener_id_ = FfiClient::instance().addListener([this](const proto::FfiEvent& e) { onEvent(e); });
  }

  ~FakeRoom() {
    // Mirror ~Room: removeListener() blocks until any in-flight callback
    // for this listener finishes, so onEvent() below can never run against
    // a destroyed FakeRoom.
    FfiClient::instance().removeListener(listener_id_);
    magic_ = kDead;
  }

  FakeRoom(const FakeRoom&) = delete;
  FakeRoom& operator=(const FakeRoom&) = delete;
  FakeRoom(FakeRoom&&) = delete;
  FakeRoom& operator=(FakeRoom&&) = delete;

  void setOnEntered(std::function<void()> fn) { on_entered_ = std::move(fn); }
  void setReleaseGate(std::shared_future<void> gate) { gate_ = std::move(gate); }
  int events() const { return events_.load(); }

private:
  void onEvent(const proto::FfiEvent&) {
    // If `this` were freed mid-dispatch, these reads would observe kDead or
    // garbage (and trip ASan); the listener handshake must keep us alive.
    EXPECT_EQ(magic_, kAlive) << "onEvent ran against a destroyed FakeRoom (use-after-free)";
    if (on_entered_) {
      on_entered_();
    }
    if (gate_.valid()) {
      gate_.wait();
    }
    EXPECT_EQ(magic_, kAlive) << "FakeRoom freed while onEvent was still running";
    ++events_;
  }

  std::uint32_t magic_ = kAlive;
  FfiClient::ListenerId listener_id_ = 0;
  std::function<void()> on_entered_;
  std::shared_future<void> gate_;
  std::atomic<int> events_{0};
};

} // namespace

class FfiClientTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Ensure the singleton for this test case starts up uninitialized
    livekit::shutdown();

    // This assert helps test the livekit::shutdown() <-> FFI client interface
    ASSERT_FALSE(FfiClient::instance().isInitialized());
  }

  void TearDown() override { livekit::shutdown(); }
};

TEST_F(FfiClientTest, Singleton) {
  auto& a = FfiClient::instance();
  auto& b = FfiClient::instance();
  EXPECT_EQ(&a, &b);
}

// ---------------------------------------------------------------------------
// Initialization state
// ---------------------------------------------------------------------------

TEST_F(FfiClientTest, DefaultUninitialized) { EXPECT_FALSE(FfiClient::instance().isInitialized()); }

TEST_F(FfiClientTest, Initialize) {
  EXPECT_TRUE(FfiClient::instance().initialize(false));
  EXPECT_TRUE(FfiClient::instance().isInitialized());
}

TEST_F(FfiClientTest, InitializeFromSDK) {
  EXPECT_TRUE(livekit::initialize(livekit::LogLevel::Info));
  EXPECT_TRUE(FfiClient::instance().isInitialized());
}

TEST_F(FfiClientTest, DoubleInitialize) {
  ASSERT_TRUE(FfiClient::instance().initialize(false));
  EXPECT_FALSE(FfiClient::instance().initialize(false))
      << "second initialize() on an already-initialized client must be a no-op";
  EXPECT_TRUE(FfiClient::instance().isInitialized());
}

TEST_F(FfiClientTest, Shutdown) {
  ASSERT_TRUE(FfiClient::instance().initialize(false));
  ASSERT_TRUE(FfiClient::instance().isInitialized());

  FfiClient::instance().shutdown();
  EXPECT_FALSE(FfiClient::instance().isInitialized());
}

TEST_F(FfiClientTest, ShutdownWithoutInitialize) {
  EXPECT_NO_THROW(FfiClient::instance().shutdown());
  EXPECT_FALSE(FfiClient::instance().isInitialized());
}

TEST_F(FfiClientTest, RepeatedShutdown) {
  FfiClient::instance().initialize(false);
  EXPECT_NO_THROW(FfiClient::instance().shutdown());
  EXPECT_NO_THROW(FfiClient::instance().shutdown());
  EXPECT_NO_THROW(FfiClient::instance().shutdown());
}

TEST_F(FfiClientTest, ReinitializeAfterShutdown) {
  ASSERT_TRUE(FfiClient::instance().initialize(false));
  FfiClient::instance().shutdown();
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  EXPECT_TRUE(FfiClient::instance().initialize(false));
  EXPECT_TRUE(FfiClient::instance().isInitialized());
}

// ---------------------------------------------------------------------------
// addListener / removeListener
// ---------------------------------------------------------------------------

TEST_F(FfiClientTest, AddListenerReturnsNonZeroId) {
  const auto id = FfiClient::instance().addListener([](const proto::FfiEvent&) {});
  EXPECT_NE(id, 0);
  FfiClient::instance().removeListener(id);
}

TEST_F(FfiClientTest, AddListenerReturnsUniqueIds) {
  constexpr int kCount = 16;
  std::unordered_set<FfiClient::ListenerId> ids;
  ids.reserve(kCount);
  for (int i = 0; i < kCount; ++i) {
    const auto id = FfiClient::instance().addListener([](const proto::FfiEvent&) {});
    EXPECT_TRUE(ids.insert(id).second) << "duplicate listener id: " << id;
  }
  for (auto id : ids) {
    FfiClient::instance().removeListener(id);
  }
}

TEST_F(FfiClientTest, RemoveListenerWithUnknownIdIsSafe) {
  EXPECT_NO_THROW(FfiClient::instance().removeListener(424242));
}

TEST_F(FfiClientTest, RemoveListenerIsIdempotent) {
  const auto id = FfiClient::instance().addListener([](const proto::FfiEvent&) {});
  EXPECT_NO_THROW(FfiClient::instance().removeListener(id));
  EXPECT_NO_THROW(FfiClient::instance().removeListener(id));
}

TEST_F(FfiClientTest, ShutdownClearsListenerRegistrations) {
  FfiClient::instance().initialize(false);
  std::atomic<int> listener_calls{0};
  const auto id = FfiClient::instance().addListener([&listener_calls](const proto::FfiEvent&) { ++listener_calls; });
  EXPECT_NE(id, 0);

  FfiClient::instance().shutdown();
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  ASSERT_TRUE(FfiClient::instance().initialize(false));
  emitEvent();
  EXPECT_EQ(listener_calls.load(), 0);
}

TEST_F(FfiClientTest, RemoveListenerWaitsForInFlightCallback) {
  ASSERT_TRUE(FfiClient::instance().initialize(false));

  std::promise<void> callback_entered;
  auto callback_entered_future = callback_entered.get_future();
  std::promise<void> release_callback;
  auto release_callback_future = release_callback.get_future();
  std::atomic<bool> callback_completed{false};

  const auto id = FfiClient::instance().addListener([&](const proto::FfiEvent&) {
    callback_entered.set_value();
    release_callback_future.wait();
    callback_completed.store(true);
  });

  std::thread callback_thread([] { emitEvent(); });
  ASSERT_EQ(callback_entered_future.wait_for(kListenerSyncTimeout), std::future_status::ready);

  auto remove_future = std::async(std::launch::async, [&] { FfiClient::instance().removeListener(id); });
  EXPECT_EQ(remove_future.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
  EXPECT_FALSE(callback_completed.load());

  release_callback.set_value();
  callback_thread.join();

  EXPECT_EQ(remove_future.wait_for(kListenerSyncTimeout), std::future_status::ready);
  EXPECT_TRUE(callback_completed.load());
}

// Reproduces the reported "Room event vs. Room destruction" race: the FFI
// thread is inside the listener callback (dereferencing `this`) at the exact
// moment the owning object is destroyed on another thread. ~FakeRoom() ->
// removeListener() must block until the in-flight callback returns, so the
// callback never touches freed memory. Without the ListenerSlot handshake the
// destroy thread would free the FakeRoom while onEvent() is still running.
TEST_F(FfiClientTest, RoomDestructionRace) {
  ASSERT_TRUE(FfiClient::instance().initialize(false));

  std::promise<void> callback_entered;
  auto callback_entered_future = callback_entered.get_future();
  std::promise<void> release_callback;
  const std::shared_future<void> release_callback_future = release_callback.get_future().share();
  std::atomic<bool> entered_once{false};

  auto room = std::make_unique<FakeRoom>();
  room->setReleaseGate(release_callback_future);
  room->setOnEntered([&] {
    if (!entered_once.exchange(true)) {
      callback_entered.set_value();
    }
  });

  // FFI thread dispatches an event; FakeRoom::onEvent is now parked inside the
  // callback holding `this`, waiting on the release gate.
  std::thread ffi_thread([] { emitEvent(); });
  ASSERT_EQ(callback_entered_future.wait_for(kListenerSyncTimeout), std::future_status::ready);

  // Destroy the owner on a different thread while the callback is in flight.
  std::atomic<bool> destroyed{false};
  std::thread destroy_thread([&] {
    room.reset();
    destroyed.store(true);
  });

  // The destructor (removeListener) must block while the callback holds the
  // slot; the FakeRoom must still be alive.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(destroyed.load()) << "destruction completed while a callback was still running";

  // Let the callback finish; destruction should now unblock and complete.
  release_callback.set_value();
  ffi_thread.join();
  destroy_thread.join();
  EXPECT_TRUE(destroyed.load());
}

// Same race exercised under contention: repeatedly create a FakeRoom while a
// background thread floods events, then destroy it. The destroy can land
// before, during, or after dispatch, sweeping the (A) copy-pointer / (B)
// invoke-onEvent window the report describes. Any use-after-free trips the
// magic-sentinel assertions in FakeRoom::onEvent (and ASan, if enabled).
TEST_F(FfiClientTest, RoomDestructionRaceFloodEvents) {
  ASSERT_TRUE(FfiClient::instance().initialize(false));

  std::atomic<bool> stop{false};
  std::thread emitter([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      emitEvent();
    }
  });

  constexpr int kIterations = 500;
  for (int i = 0; i < kIterations; ++i) {
    auto room = std::make_unique<FakeRoom>();
    // Give the emitter a chance to dispatch into this listener before we tear
    // it down, so destruction races against an active/just-finishing callback.
    std::this_thread::yield();
    room.reset(); // ~FakeRoom -> removeListener must drain safely.
  }

  stop.store(true, std::memory_order_relaxed);
  emitter.join();
}

TEST_F(FfiClientTest, ShutdownFromListenerDoesNotDeadlock) {
  ASSERT_TRUE(FfiClient::instance().initialize(false));

  std::atomic<bool> shutdown_returned{false};
  const auto id = FfiClient::instance().addListener([&shutdown_returned](const proto::FfiEvent&) {
    FfiClient::instance().shutdown();
    shutdown_returned.store(true);
  });
  ASSERT_NE(id, 0);

  auto callback_future = std::async(std::launch::async, [] { emitEvent(); });
  EXPECT_EQ(callback_future.wait_for(kListenerSyncTimeout), std::future_status::ready);
  EXPECT_TRUE(shutdown_returned.load());
  EXPECT_FALSE(FfiClient::instance().isInitialized());
}

TEST_F(FfiClientTest, ShutdownRejectsReinitializeAndDropsNewEventsWhileDraining) {
  ASSERT_TRUE(FfiClient::instance().initialize(false));

  std::promise<void> callback_entered;
  auto callback_entered_future = callback_entered.get_future();
  std::promise<void> release_callback;
  auto release_callback_future = release_callback.get_future();
  std::atomic<int> listener_calls{0};

  const auto id = FfiClient::instance().addListener([&](const proto::FfiEvent&) {
    ++listener_calls;
    callback_entered.set_value();
    release_callback_future.wait();
  });
  ASSERT_NE(id, 0);

  std::thread callback_thread([] { emitEvent(); });
  ASSERT_EQ(callback_entered_future.wait_for(kListenerSyncTimeout), std::future_status::ready);

  auto shutdown_future = std::async(std::launch::async, [] { FfiClient::instance().shutdown(); });
  for (int i = 0; i < 5000 && FfiClient::instance().isInitialized(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  EXPECT_EQ(shutdown_future.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
  EXPECT_FALSE(FfiClient::instance().initialize(false));

  emitEvent();
  EXPECT_EQ(listener_calls.load(), 1);

  release_callback.set_value();
  callback_thread.join();
  EXPECT_EQ(shutdown_future.wait_for(kListenerSyncTimeout), std::future_status::ready);
  EXPECT_FALSE(FfiClient::instance().isInitialized());
}

TEST_F(FfiClientTest, PanicEvent) {
  // Wire up a signal handler to ensure the panic event raises SIGTERM
  // (and that users can handle it)
  g_sigterm_received = false;
  auto previous_handler = std::signal(SIGTERM, handleSignal);
  ASSERT_NE(previous_handler, SIG_ERR);

  // Wire up a listener to ensure the panic event doesn't make it through
  // (matches Python SDK)
  bool listener_called = false;
  const auto id =
      FfiClient::instance().addListener([&listener_called](const proto::FfiEvent&) { listener_called = true; });

  proto::FfiEvent event;
  event.mutable_panic()->set_message("rust panic");

  testing::internal::CaptureStderr();
  emitFfiEvent(event);
  const std::string stderr_output = testing::internal::GetCapturedStderr();

  ASSERT_NE(std::signal(SIGTERM, previous_handler), SIG_ERR);
  FfiClient::instance().removeListener(id);

  EXPECT_TRUE(g_sigterm_received);
  EXPECT_FALSE(listener_called);
  EXPECT_NE(stderr_output.find("FFI Panic: rust panic"), std::string::npos);
}

// ---------------------------------------------------------------------------
// These tests ensure FfiClient methods throw in various error conditions
// ---------------------------------------------------------------------------

TEST_F(FfiClientTest, SendRequestThrowsOnEmptyRequest) {
  // A default-constructed FfiRequest has no oneof populated and serializes
  // to zero bytes, which sendRequest treats as a serialization failure.
  // This path is reachable regardless of initialization state.
  proto::FfiRequest req;
  EXPECT_THROW(FfiClient::instance().sendRequest(req), std::runtime_error);
}

TEST_F(FfiClientTest, SendRequestThrowsAfterShutdown) {
  ASSERT_TRUE(FfiClient::instance().initialize(false));
  FfiClient::instance().shutdown();
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  proto::FfiRequest req;
  (void)req.mutable_dispose();

  EXPECT_THROW(FfiClient::instance().sendRequest(req), std::runtime_error);
}

TEST_F(FfiClientTest, NotInitialized_ConnectAsyncThrows) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  RoomOptions options;
  EXPECT_THROW(FfiClient::instance().connectAsync("wss://localhost:7880", "fake-token", options), std::runtime_error);
}

TEST_F(FfiClientTest, NotInitialized_PublishTrackAsyncThrows) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  TrackPublishOptions options;
  EXPECT_THROW(FfiClient::instance().publishTrackAsync(1, 2, options), std::runtime_error);
}

TEST_F(FfiClientTest, NotInitialized_UnpublishTrackAsyncThrows) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  EXPECT_THROW(FfiClient::instance().unpublishTrackAsync(1, "sid", true), std::runtime_error);
}

TEST_F(FfiClientTest, NotInitialized_PublishDataAsyncThrows) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  const std::uint8_t payload[1] = {0};
  EXPECT_THROW(FfiClient::instance().publishDataAsync(1, payload, 1, true, {}, ""), std::runtime_error);
}

TEST_F(FfiClientTest, NotInitialized_PublishSipDtmfAsyncThrows) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  EXPECT_THROW(FfiClient::instance().publishSipDtmfAsync(1, 1, "1", {}), std::runtime_error);
}

TEST_F(FfiClientTest, NotInitialized_SetLocalMetadataAsyncThrows) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  EXPECT_THROW(FfiClient::instance().setLocalMetadataAsync(1, "metadata"), std::runtime_error);
}

TEST_F(FfiClientTest, NotInitialized_CaptureAudioFrameAsyncThrows) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  proto::AudioFrameBufferInfo buf;
  EXPECT_THROW(FfiClient::instance().captureAudioFrameAsync(1, buf), std::runtime_error);
}

TEST_F(FfiClientTest, NotInitialized_PerformRpcAsyncThrows) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  EXPECT_THROW(FfiClient::instance().performRpcAsync(1, "dest", "method", "payload", std::nullopt), std::runtime_error);
}

TEST_F(FfiClientTest, NotInitialized_GetTrackStatsAsyncThrows) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  EXPECT_THROW(FfiClient::instance().getTrackStatsAsync(1), std::runtime_error);
}

TEST_F(FfiClientTest, NotInitialized_GetSessionStatsAsyncThrows) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  EXPECT_THROW(FfiClient::instance().getSessionStatsAsync(1), std::runtime_error);
}

TEST_F(FfiClientTest, NotInitialized_PublishDataTrackAsyncFails) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  DataTrackPublishOptions options;
  options.name = "name";
  auto fut_result = FfiClient::instance().publishDataTrackAsync(1, options);
  auto result = fut_result.get();
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error().code, PublishDataTrackErrorCode::INTERNAL);
}

TEST_F(FfiClientTest, NotInitialized_DefineSchemaAsyncFails) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  const DataTrackSchemaId schema_id{"schema", DataTrackSchemaEncoding::JsonSchema};
  auto result = FfiClient::instance().defineSchemaAsync(1, schema_id, "{}").get();
  EXPECT_FALSE(result);
  EXPECT_FALSE(result.error().empty());
}

TEST_F(FfiClientTest, NotInitialized_GetSchemaAsyncFails) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  const DataTrackSchemaId schema_id{"schema", DataTrackSchemaEncoding::JsonSchema};
  auto result = FfiClient::instance().getSchemaAsync(1, schema_id, "participant").get();
  EXPECT_FALSE(result);
  EXPECT_FALSE(result.error().empty());
}

TEST_F(FfiClientTest, NotInitialized_SubscribeDataTrackFails) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  auto result = FfiClient::instance().subscribeDataTrack(1);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error().code, SubscribeDataTrackErrorCode::INTERNAL);
}

TEST_F(FfiClientTest, NotInitialized_SendStreamHeaderAsyncThrows) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  proto::DataStream::Header header;
  EXPECT_THROW(FfiClient::instance().sendStreamHeaderAsync(1, header, {}, "sender"), std::runtime_error);
}

TEST_F(FfiClientTest, NotInitialized_SendStreamChunkAsyncThrows) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  proto::DataStream::Chunk chunk;
  EXPECT_THROW(FfiClient::instance().sendStreamChunkAsync(1, chunk, {}, "sender"), std::runtime_error);
}

TEST_F(FfiClientTest, NotInitialized_SendStreamTrailerAsyncThrows) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  proto::DataStream::Trailer trailer;
  EXPECT_THROW(FfiClient::instance().sendStreamTrailerAsync(1, trailer, "sender"), std::runtime_error);
}

} // namespace livekit::test
