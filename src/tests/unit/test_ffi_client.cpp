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

TEST_F(FfiClientTest, ListenerRegistrationSurvivesShutdownReinitCycle) {
  FfiClient::instance().initialize(false);
  const auto id = FfiClient::instance().addListener([](const proto::FfiEvent&) {});
  EXPECT_NE(id, 0);

  // shutdown() does not clear the C++-side listener map today; document that
  // contract here so a future refactor that changes it is a deliberate choice.
  FfiClient::instance().shutdown();
  EXPECT_NO_THROW(FfiClient::instance().removeListener(id));
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

TEST_F(FfiClientTest, NotInitialized_GetSessionStatsAsyncFails) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());

  auto fut_result = FfiClient::instance().getSessionStatsAsync(1);
  auto result = fut_result.get();
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(result.error().empty());
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
