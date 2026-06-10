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

#include "ffi.pb.h"
#include "ffi_client.h"

namespace livekit::test {

namespace {

volatile bool g_sigterm_received = false;

// Has to be registered globally per csignal API
void handleSignal(int signal) {
  if (signal == SIGTERM) {
    g_sigterm_received = true;
  }
}

void emitLogEvent() {
  proto::FfiEvent event;
  auto* record = event.mutable_logs()->add_records();
  record->set_level(proto::LOG_INFO);
  record->set_target("test");
  record->set_message("listener event");

  std::string bytes;
  ASSERT_TRUE(event.SerializeToString(&bytes));
  ffiEventCallback(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

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
  emitLogEvent();
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

  std::thread callback_thread([] { emitLogEvent(); });
  ASSERT_EQ(callback_entered_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);

  auto remove_future = std::async(std::launch::async, [&] { FfiClient::instance().removeListener(id); });
  EXPECT_EQ(remove_future.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
  EXPECT_FALSE(callback_completed.load());

  release_callback.set_value();
  callback_thread.join();

  EXPECT_EQ(remove_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  EXPECT_TRUE(callback_completed.load());
}

TEST_F(FfiClientTest, ShutdownFromListenerDoesNotDeadlock) {
  ASSERT_TRUE(FfiClient::instance().initialize(false));

  std::atomic<bool> shutdown_returned{false};
  const auto id = FfiClient::instance().addListener([&shutdown_returned](const proto::FfiEvent&) {
    FfiClient::instance().shutdown();
    shutdown_returned.store(true);
  });
  ASSERT_NE(id, 0);

  auto callback_future = std::async(std::launch::async, [] { emitLogEvent(); });
  EXPECT_EQ(callback_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
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

  std::thread callback_thread([] { emitLogEvent(); });
  ASSERT_EQ(callback_entered_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);

  auto shutdown_future = std::async(std::launch::async, [] { FfiClient::instance().shutdown(); });
  for (int i = 0; i < 5000 && FfiClient::instance().isInitialized(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  EXPECT_EQ(shutdown_future.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
  EXPECT_FALSE(FfiClient::instance().initialize(false));

  emitLogEvent();
  EXPECT_EQ(listener_calls.load(), 1);

  release_callback.set_value();
  callback_thread.join();
  EXPECT_EQ(shutdown_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
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
  std::string bytes;
  ASSERT_TRUE(event.SerializeToString(&bytes));

  testing::internal::CaptureStderr();
  ffiEventCallback(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
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

  auto fut_result = FfiClient::instance().publishDataTrackAsync(1, "name");
  auto result = fut_result.get();
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error().code, PublishDataTrackErrorCode::INTERNAL);
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
