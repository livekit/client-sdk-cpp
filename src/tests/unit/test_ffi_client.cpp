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

/// @file test_ffi_client.cpp
/// @brief Unit tests for FfiClient (the internal singleton that bridges the
///        C++ SDK to the Rust FFI runtime).
///
/// Scope:
///   - Singleton identity
///   - initialize() / shutdown() / isInitialized() state machine
///   - AddListener() / RemoveListener() lifecycle and ID uniqueness
///   - sendRequest() invariants that are reachable without a live FFI
///     roundtrip (empty-serialization failure path)
///
/// Out of scope (covered by integration tests with a live Rust runtime):
///   - Real sendRequest()/sendRequestAsync() responses
///   - Listener invocation from the FFI callback thread
///
/// DISABLED_ tests in this file are stubs for the "isInitialized() gate"
/// design proposal (see project notes). Strip the DISABLED_ prefix once
/// the corresponding gate lands in production.

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <stdexcept>
#include <unordered_set>

#include "ffi.pb.h"
#include "ffi_client.h"

namespace livekit::test {

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
  EXPECT_TRUE(livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole));
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
// AddListener / RemoveListener
// ---------------------------------------------------------------------------

TEST_F(FfiClientTest, AddListenerReturnsNonZeroId) {
  const auto id = FfiClient::instance().AddListener([](const proto::FfiEvent&) {});
  EXPECT_NE(id, 0);
  FfiClient::instance().RemoveListener(id);
}

TEST_F(FfiClientTest, AddListenerReturnsUniqueIds) {
  constexpr int kCount = 16;
  std::unordered_set<FfiClient::ListenerId> ids;
  ids.reserve(kCount);
  for (int i = 0; i < kCount; ++i) {
    const auto id = FfiClient::instance().AddListener([](const proto::FfiEvent&) {});
    EXPECT_TRUE(ids.insert(id).second) << "duplicate listener id: " << id;
  }
  for (auto id : ids) {
    FfiClient::instance().RemoveListener(id);
  }
}

TEST_F(FfiClientTest, RemoveListenerWithUnknownIdIsSafe) {
  EXPECT_NO_THROW(FfiClient::instance().RemoveListener(/*never registered=*/424242));
}

TEST_F(FfiClientTest, RemoveListenerIsIdempotent) {
  const auto id = FfiClient::instance().AddListener([](const proto::FfiEvent&) {});
  EXPECT_NO_THROW(FfiClient::instance().RemoveListener(id));
  EXPECT_NO_THROW(FfiClient::instance().RemoveListener(id));
}

TEST_F(FfiClientTest, ListenerRegistrationSurvivesShutdownReinitCycle) {
  FfiClient::instance().initialize(false);
  const auto id = FfiClient::instance().AddListener([](const proto::FfiEvent&) {});
  EXPECT_NE(id, 0);

  // shutdown() does not clear the C++-side listener map today; document that
  // contract here so a future refactor that changes it is a deliberate choice.
  FfiClient::instance().shutdown();
  EXPECT_NO_THROW(FfiClient::instance().RemoveListener(id));
}

// ---------------------------------------------------------------------------
// sendRequest() — invariants reachable without a live FFI
// ---------------------------------------------------------------------------

TEST_F(FfiClientTest, SendRequestThrowsOnEmptyRequest) {
  // A default-constructed FfiRequest has no oneof populated and serializes
  // to zero bytes, which sendRequest treats as a serialization failure.
  // This path is reachable regardless of initialization state.
  proto::FfiRequest req;
  EXPECT_THROW(FfiClient::instance().sendRequest(req), std::runtime_error);
}

TEST_F(FfiClientTest, SendRequestWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  proto::FfiRequest req;
  // Populate any oneof so we get past the empty-bytes serialization check
  // and hit the proposed init gate instead.
  (void)req.mutable_dispose();

  // EXPECT_THROW(FfiClient::instance().sendRequest(req), std::runtime_error);
}

TEST_F(FfiClientTest, DISABLED_SendRequestThrowsAfterShutdown) {
  ASSERT_TRUE(FfiClient::instance().initialize(false));
  FfiClient::instance().shutdown();
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  proto::FfiRequest req;
  (void)req.mutable_dispose();

  EXPECT_THROW(FfiClient::instance().sendRequest(req), std::runtime_error);
}

// ---------------------------------------------------------------------------
// PROPOSED: *Async helper isInitialized() gates (Tier 1 #3)
//
// Each async helper (connectAsync, publishTrackAsync, captureAudioFrameAsync,
// publishDataAsync, performRpcAsync, publishDataTrackAsync, etc.) should
// surface "not initialized" through its return channel:
//   - std::future<T> helpers: the future should resolve with an exception
//     (fut.get() throws std::runtime_error).
//   - Result<T, E> helpers (subscribeDataTrack, publishDataTrackAsync's
//     Result payload): the Result should be a failure with an appropriate
//     error code.
//
// Each stub below pins down the *contract* for one helper. They are kept
// DISABLED for the same reason as sendRequest above: without the gate, the
// helper either calls into Rust unsafely or registers a pending waiter that
// will never complete.
//
// As you add each gate, strip the DISABLED_ prefix one test at a time.
// ---------------------------------------------------------------------------

TEST_F(FfiClientTest, DISABLED_ConnectAsyncFailsWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  RoomOptions options;
  auto fut = FfiClient::instance().connectAsync("wss://localhost:7880", "fake-token", options);
  EXPECT_THROW(fut.get(), std::runtime_error);
}

TEST_F(FfiClientTest, DISABLED_PublishTrackAsyncFailsWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  // Handle values are placeholders — the gate must fail before they are
  // ever forwarded to Rust.
  // TrackPublishOptions opts;
  // auto fut = FfiClient::instance().publishTrackAsync(/*participant=*/1, /*track=*/2, opts);
  // EXPECT_THROW(fut.get(), std::runtime_error);
  GTEST_SKIP() << "TODO: enable once publishTrackAsync gate lands";
}

TEST_F(FfiClientTest, DISABLED_UnpublishTrackAsyncFailsWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  // auto fut = FfiClient::instance().unpublishTrackAsync(/*participant=*/1, "sid", true);
  // EXPECT_THROW(fut.get(), std::runtime_error);
  GTEST_SKIP() << "TODO: enable once unpublishTrackAsync gate lands";
}

TEST_F(FfiClientTest, DISABLED_PublishDataAsyncFailsWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  // const std::uint8_t payload[1] = {0};
  // auto fut = FfiClient::instance().publishDataAsync(
  //     /*participant=*/1, payload, 1, /*reliable=*/true, /*destinations=*/{}, /*topic=*/"");
  // EXPECT_THROW(fut.get(), std::runtime_error);
  GTEST_SKIP() << "TODO: enable once publishDataAsync gate lands";
}

TEST_F(FfiClientTest, DISABLED_PublishSipDtmfAsyncFailsWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  // auto fut = FfiClient::instance().publishSipDtmfAsync(/*participant=*/1, /*code=*/1, "1", {});
  // EXPECT_THROW(fut.get(), std::runtime_error);
  GTEST_SKIP() << "TODO: enable once publishSipDtmfAsync gate lands";
}

TEST_F(FfiClientTest, DISABLED_SetLocalMetadataAsyncFailsWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  // auto fut = FfiClient::instance().setLocalMetadataAsync(/*participant=*/1, "metadata");
  // EXPECT_THROW(fut.get(), std::runtime_error);
  GTEST_SKIP() << "TODO: enable once setLocalMetadataAsync gate lands";
}

TEST_F(FfiClientTest, DISABLED_CaptureAudioFrameAsyncFailsWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  // proto::AudioFrameBufferInfo buf;
  // auto fut = FfiClient::instance().captureAudioFrameAsync(/*source=*/1, buf);
  // EXPECT_THROW(fut.get(), std::runtime_error);
  GTEST_SKIP() << "TODO: enable once captureAudioFrameAsync gate lands";
}

TEST_F(FfiClientTest, DISABLED_PerformRpcAsyncFailsWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  // auto fut = FfiClient::instance().performRpcAsync(
  //     /*participant=*/1, "dest", "method", "payload", std::nullopt);
  // EXPECT_THROW(fut.get(), std::runtime_error);
  GTEST_SKIP() << "TODO: enable once performRpcAsync gate lands";
}

TEST_F(FfiClientTest, DISABLED_GetTrackStatsAsyncFailsWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  // auto fut = FfiClient::instance().getTrackStatsAsync(/*track=*/1);
  // EXPECT_THROW(fut.get(), std::runtime_error);
  GTEST_SKIP() << "TODO: enable once getTrackStatsAsync gate lands";
}

TEST_F(FfiClientTest, DISABLED_PublishDataTrackAsyncReturnsFailureWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  // publishDataTrackAsync's future yields a Result<..., PublishDataTrackError>,
  // so the convention here is "failure result" rather than a thrown exception.
  //
  // auto fut = FfiClient::instance().publishDataTrackAsync(/*participant=*/1, "name");
  // auto result = fut.get();
  // EXPECT_FALSE(result.ok());
  // EXPECT_EQ(result.error().code, PublishDataTrackErrorCode::INVALID_HANDLE);  // or NOT_INITIALIZED
  GTEST_SKIP() << "TODO: enable once publishDataTrackAsync gate lands; "
                  "decide between INVALID_HANDLE and a new NOT_INITIALIZED code";
}

TEST_F(FfiClientTest, DISABLED_SubscribeDataTrackReturnsFailureWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  // subscribeDataTrack returns Result<..., SubscribeDataTrackError> directly.
  //
  // auto result = FfiClient::instance().subscribeDataTrack(/*track=*/1);
  // EXPECT_FALSE(result.ok());
  // EXPECT_EQ(result.error().code, SubscribeDataTrackErrorCode::INVALID_HANDLE);  // or NOT_INITIALIZED
  GTEST_SKIP() << "TODO: enable once subscribeDataTrack gate lands; "
                  "decide between INVALID_HANDLE and a new NOT_INITIALIZED code";
}

TEST_F(FfiClientTest, DISABLED_SendStreamHeaderAsyncFailsWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  // proto::DataStream::Header header;
  // auto fut = FfiClient::instance().sendStreamHeaderAsync(/*participant=*/1, header, {}, "sender");
  // EXPECT_THROW(fut.get(), std::runtime_error);
  GTEST_SKIP() << "TODO: enable once sendStreamHeaderAsync gate lands";
}

TEST_F(FfiClientTest, DISABLED_SendStreamChunkAsyncFailsWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  // proto::DataStream::Chunk chunk;
  // auto fut = FfiClient::instance().sendStreamChunkAsync(/*participant=*/1, chunk, {}, "sender");
  // EXPECT_THROW(fut.get(), std::runtime_error);
  GTEST_SKIP() << "TODO: enable once sendStreamChunkAsync gate lands";
}

TEST_F(FfiClientTest, DISABLED_SendStreamTrailerAsyncFailsWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  // proto::DataStream::Trailer trailer;
  // auto fut = FfiClient::instance().sendStreamTrailerAsync(/*participant=*/1, trailer, "sender");
  // EXPECT_THROW(fut.get(), std::runtime_error);
  GTEST_SKIP() << "TODO: enable once sendStreamTrailerAsync gate lands";
}

} // namespace livekit::test
