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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <livekit/capture_source.h>
#include <livekit/track.h>

#include <chrono>
#include <thread>

#include "../common/ffi_utils.h"
#include "capture.pb.h"
#include "capture_source_internal.h"
#include "ffi_client.h"
#include "livekit_ffi.h"

namespace livekit {

struct FfiClientTestAccess {
  static std::size_t listenerCount() {
    auto& client = FfiClient::instance();
    const std::scoped_lock lock(client.lock_);
    return client.listeners_.size();
  }

  static std::size_t pendingCount() {
    auto& client = FfiClient::instance();
    const std::scoped_lock lock(client.lock_);
    return client.pending_by_id_.size();
  }
};

} // namespace livekit

namespace livekit::test {
namespace {

// Zero handles stand in for Rust resources: these tests exercise ownership of
// the C++ wrapper and its listener without starting a capture producer.
proto::OwnedCaptureSource makeOwned(proto::CaptureSourceKind kind = proto::CAPTURE_SOURCE_PIXEL) {
  proto::OwnedCaptureSource owned;
  owned.mutable_handle()->set_id(0);
  auto* info = owned.mutable_info();
  info->set_kind(kind);
  info->mutable_resolution()->set_width(1280);
  info->mutable_resolution()->set_height(720);
  auto* video = info->mutable_video_source();
  video->mutable_handle()->set_id(0);
  video->mutable_info()->set_type(kind == proto::CAPTURE_SOURCE_ENCODED ? proto::VIDEO_SOURCE_ENCODED
                                                                        : proto::VIDEO_SOURCE_NATIVE);
  auto* recommended = info->mutable_recommended_publish_options();
  recommended->set_video_codec(proto::VP8);
  recommended->set_simulcast(true);
  if (kind == proto::CAPTURE_SOURCE_ENCODED) {
    info->set_codec(proto::H264);
    recommended->set_video_codec(proto::H264);
    recommended->set_video_encoder(proto::ENCODER_BACKEND_PRE_ENCODED);
    recommended->set_simulcast(false);
  }
  return owned;
}

proto::FfiEvent creationEvent(std::uint64_t async_id, const proto::OwnedCaptureSource& owned) {
  proto::FfiEvent event;
  event.mutable_new_capture_source()->set_async_id(async_id);
  event.mutable_new_capture_source()->mutable_source()->CopyFrom(owned);
  return event;
}

proto::OwnedVideoSource newNativeVideoSource() {
  proto::FfiRequest request;
  auto* video = request.mutable_new_video_source();
  video->set_type(proto::VIDEO_SOURCE_NATIVE);
  video->mutable_resolution()->set_width(1280);
  video->mutable_resolution()->set_height(720);
  return FfiClient::instance().sendRequest(request).new_video_source().source();
}

class CaptureSourceCallbackTest : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(FfiClient::instance().initialize(false)); }
  void TearDown() override { FfiClient::instance().shutdown(); }

  std::future<std::shared_ptr<CaptureSource>> beginCreation() {
    return FfiClient::instance().newCaptureSourceAsync({}, [this](const proto::FfiRequest& request) {
      async_id_ = request.new_capture_source().request_async_id();
      proto::FfiResponse response;
      response.mutable_new_capture_source()->set_async_id(async_id_);
      return response;
    });
  }

  std::shared_ptr<CaptureSource> completeCreation(const proto::OwnedCaptureSource& owned) {
    auto future = beginCreation();
    emitFfiEvent(creationEvent(async_id_, owned));
    return future.get();
  }

  std::uint64_t async_id_ = 0;
};

TEST_F(CaptureSourceCallbackTest, AbandonedFutureDoesNotWaitForCallbackAndReleasesWrapper) {
  auto future = beginCreation();
  std::promise<void> discarded;
  auto discarded_future = discarded.get_future();
  std::thread destroyer([future = std::move(future), &discarded]() mutable {
    future = {};
    discarded.set_value();
  });
  // A generous failure bound, not a latency assertion. Complete the callback
  // even on failure so that a regression cannot leave the worker hanging.
  EXPECT_EQ(discarded_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  emitFfiEvent(creationEvent(async_id_, makeOwned()));
  destroyer.join();
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
  EXPECT_EQ(FfiClientTestAccess::listenerCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, CallbackReturnsWrapperAndOwnsVideoSource) {
  auto source = completeCreation(makeOwned());
  ASSERT_NE(source, nullptr);
  EXPECT_EQ(source->kind(), CaptureSourceKind::Pixel);
  EXPECT_EQ(source->width(), 1280);
  EXPECT_EQ(source->height(), 720);
  EXPECT_EQ(FfiClientTestAccess::listenerCount(), 1u);
  std::weak_ptr<CaptureSource> weak_source = source;
  std::weak_ptr<VideoSource> weak_video = source->videoSource();
  source.reset();
  EXPECT_TRUE(weak_source.expired());
  EXPECT_TRUE(weak_video.expired());
  EXPECT_EQ(FfiClientTestAccess::listenerCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, AbandonedAndInvalidResultsReleaseBothRustHandles) {
  for (const bool invalid : {false, true}) {
    auto owned = makeOwned(proto::CAPTURE_SOURCE_ENCODED);
    // The capture handle can be any Rust-owned resource in this synthetic
    // callback; disposal is type-independent. Native video sources need no GPU.
    const auto capture = newNativeVideoSource();
    const auto video = newNativeVideoSource();
    FfiHandle capture_guard(static_cast<uintptr_t>(capture.handle().id()));
    FfiHandle video_guard(static_cast<uintptr_t>(video.handle().id()));
    owned.mutable_handle()->CopyFrom(capture.handle());
    owned.mutable_info()->mutable_video_source()->CopyFrom(video);
    if (invalid) {
      owned.mutable_info()->clear_codec();
    }
    auto future = beginCreation();
    if (!invalid) {
      future = {};
    }
    static_cast<void>(capture_guard.release());
    static_cast<void>(video_guard.release());
    emitFfiEvent(creationEvent(async_id_, owned));
    if (invalid) {
      EXPECT_THROW(static_cast<void>(future.get()), CaptureSourceError);
    }
    // A second drop must fail. If ownership leaked, this also cleans it up.
    EXPECT_FALSE(livekit_ffi_drop_handle(capture.handle().id()));
    EXPECT_FALSE(livekit_ffi_drop_handle(video.handle().id()));
    EXPECT_EQ(FfiClientTestAccess::listenerCount(), 0u);
  }
}

TEST_F(CaptureSourceCallbackTest, CallbackMayCompleteBeforeSendReturns) {
  auto future = FfiClient::instance().newCaptureSourceAsync({}, [](const proto::FfiRequest& request) {
    const auto async_id = request.new_capture_source().request_async_id();
    emitFfiEvent(creationEvent(async_id, makeOwned()));
    proto::FfiResponse response;
    response.mutable_new_capture_source()->set_async_id(async_id);
    return response;
  });
  EXPECT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
  EXPECT_NE(future.get(), nullptr);
}

TEST_F(CaptureSourceCallbackTest, CreationErrorUsesCaptureSourceError) {
  auto future = beginCreation();
  proto::FfiEvent event;
  event.mutable_new_capture_source()->set_async_id(async_id_);
  event.mutable_new_capture_source()->set_error("discovery failed");
  emitFfiEvent(event);
  EXPECT_THROW(static_cast<void>(future.get()), CaptureSourceError);
}

TEST_F(CaptureSourceCallbackTest, UnrelatedCallbackDoesNotCompleteFuture) {
  auto future = beginCreation();
  proto::FfiEvent event;
  event.mutable_new_capture_source()->set_async_id(async_id_ + 1);
  event.mutable_new_capture_source()->set_error("unrelated failure");
  emitFfiEvent(event);
  EXPECT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::timeout);
  emitFfiEvent(creationEvent(async_id_, makeOwned()));
  EXPECT_NE(future.get(), nullptr);
}

TEST_F(CaptureSourceCallbackTest, MissingSourceUsesCaptureSourceError) {
  auto future = beginCreation();
  proto::FfiEvent event;
  event.mutable_new_capture_source()->set_async_id(async_id_);
  emitFfiEvent(event);
  EXPECT_THROW(static_cast<void>(future.get()), CaptureSourceError);
}

TEST_F(CaptureSourceCallbackTest, InvalidEncodedInfoFailsWithoutRetainingListener) {
  auto future = beginCreation();
  auto owned = makeOwned(proto::CAPTURE_SOURCE_ENCODED);
  owned.mutable_info()->clear_codec();
  emitFfiEvent(creationEvent(async_id_, owned));
  EXPECT_THROW(static_cast<void>(future.get()), CaptureSourceError);
  EXPECT_EQ(FfiClientTestAccess::listenerCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, ShutdownCancelsPendingFutureWithCaptureSourceError) {
  auto future = beginCreation();
  FfiClient::instance().shutdown();
  EXPECT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
  EXPECT_THROW(static_cast<void>(future.get()), CaptureSourceError);
}

TEST_F(CaptureSourceCallbackTest, SendFailureRemovesPendingOperation) {
  EXPECT_THROW(FfiClient::instance().newCaptureSourceAsync(
                   {}, [](const proto::FfiRequest&) -> proto::FfiResponse { throw std::runtime_error("send failed"); }),
               std::runtime_error);
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, PixelPreservesCodecEncoderAndDisabledSimulcast) {
  auto source = completeCreation(makeOwned());
  TrackPublishOptions application;
  application.video_codec = VideoCodec::H264;
  application.video_encoder = VideoEncoderBackend::Hardware;
  application.simulcast = false;
  const auto options = source->publishOptions(application);
  EXPECT_EQ(options.video_codec, application.video_codec);
  EXPECT_EQ(options.video_encoder, application.video_encoder);
  EXPECT_EQ(options.simulcast, application.simulcast);
}

TEST_F(CaptureSourceCallbackTest, PixelLeavesDefaultsUnset) {
  auto source = completeCreation(makeOwned());
  const auto options = source->publishOptions();
  EXPECT_FALSE(options.video_codec.has_value());
  EXPECT_FALSE(options.video_encoder.has_value());
  EXPECT_FALSE(options.simulcast.has_value());
}

TEST_F(CaptureSourceCallbackTest, EncodedEnforcesCodecPassthroughAndNoSimulcast) {
  auto source = completeCreation(makeOwned(proto::CAPTURE_SOURCE_ENCODED));
  TrackPublishOptions application;
  application.video_codec = VideoCodec::VP9;
  application.video_encoder = VideoEncoderBackend::Hardware;
  application.simulcast = true;
  for (const auto& options : {source->publishOptions(application), source->publishOptions()}) {
    EXPECT_EQ(options.video_codec, VideoCodec::H264);
    EXPECT_EQ(options.video_encoder, VideoEncoderBackend::PreEncoded);
    EXPECT_EQ(options.simulcast, false);
  }
}

TEST_F(CaptureSourceCallbackTest, RecommendationsCannotReplaceOtherApplicationOptions) {
  for (const auto kind : {proto::CAPTURE_SOURCE_PIXEL, proto::CAPTURE_SOURCE_ENCODED}) {
    auto owned = makeOwned(kind);
    auto* recommended = owned.mutable_info()->mutable_recommended_publish_options();
    recommended->set_stream("camera");
    recommended->set_source(proto::SOURCE_CAMERA);
    recommended->mutable_video_encoding()->set_max_bitrate(100000);
    recommended->mutable_video_encoding()->set_max_framerate(15);
    auto source = completeCreation(owned);
    TrackPublishOptions application;
    application.stream = "presentation";
    application.source = TrackSource::SOURCE_SCREENSHARE;
    application.video_encoding = VideoEncodingOptions{2000000, 30};
    application.audio_encoding = AudioEncodingOptions{64000};
    application.dtx = false;
    application.red = false;
    application.preconnect_buffer = true;
    application.frame_metadata_features = FrameMetadataFeatures{true, true, true};
    application.degradation_preference = DegradationPreference::MaintainResolution;
    const auto options = source->publishOptions(application);
    EXPECT_EQ(options.stream, application.stream);
    EXPECT_EQ(options.source, application.source);
    ASSERT_TRUE(options.video_encoding.has_value());
    EXPECT_EQ(options.video_encoding->max_bitrate, 2000000u);
    EXPECT_EQ(options.video_encoding->max_framerate, 30);
    ASSERT_TRUE(options.audio_encoding.has_value());
    EXPECT_EQ(options.audio_encoding->max_bitrate, 64000u);
    EXPECT_EQ(options.dtx, application.dtx);
    EXPECT_EQ(options.red, application.red);
    EXPECT_EQ(options.preconnect_buffer, application.preconnect_buffer);
    ASSERT_TRUE(options.frame_metadata_features.has_value());
    EXPECT_TRUE(options.frame_metadata_features->user_timestamp);
    EXPECT_TRUE(options.frame_metadata_features->frame_id);
    EXPECT_TRUE(options.frame_metadata_features->user_data);
    EXPECT_EQ(options.degradation_preference, application.degradation_preference);
  }
}

TEST(CaptureSourceProtoTest, ClockConfigurationUsesMainlineWireContract) {
  ClockVideoSourceConfig config;
  config.resolution = {1920, 1080};
  config.framerate_fps = 30;

  const auto request = toProto(config);
  ASSERT_TRUE(request.IsInitialized());
  ASSERT_TRUE(request.has_clock());
  EXPECT_EQ(request.clock().resolution().width(), 1920u);
  EXPECT_EQ(request.clock().resolution().height(), 1080u);
  EXPECT_EQ(request.clock().framerate_fps(), 30u);
  EXPECT_FALSE(request.has_gstreamer());
  EXPECT_FALSE(request.has_pattern());
}

TEST(CaptureSourceProtoTest, RejectsNonPositiveDimensionsBeforeUnsignedConversion) {
  for (const CaptureResolution resolution : {CaptureResolution{0, 720}, CaptureResolution{-1, 720},
                                             CaptureResolution{1280, 0}, CaptureResolution{1280, -1}}) {
    PatternVideoSourceConfig pattern;
    pattern.resolution = resolution;
    pattern.framerate_fps = 30;
    EXPECT_THROW(toProto(pattern), CaptureSourceError);
    ClockVideoSourceConfig clock;
    clock.resolution = resolution;
    clock.framerate_fps = 30;
    EXPECT_THROW(toProto(clock), CaptureSourceError);
    GstreamerVideoSourceConfig gstreamer;
    gstreamer.resolution = resolution;
    EXPECT_THROW(toProto(gstreamer), CaptureSourceError);
  }
}

TEST(CaptureSourceProtoTest, RejectsZeroFrameRate) {
  PatternVideoSourceConfig pattern;
  pattern.resolution = {1280, 720};
  EXPECT_THROW(toProto(pattern), CaptureSourceError);
  ClockVideoSourceConfig clock;
  clock.resolution = {1280, 720};
  EXPECT_THROW(toProto(clock), CaptureSourceError);
}

TEST(CaptureSourceProtoTest, ValidationErrorsAreDeliveredThroughFuture) {
  std::future<std::shared_ptr<CaptureSource>> future;
  EXPECT_NO_THROW(future = CaptureSource::create(ClockVideoSourceConfig{}));
  ASSERT_TRUE(future.valid());
  EXPECT_THROW(static_cast<void>(future.get()), CaptureSourceError);
}

TEST(CaptureSourceProtoTest, SendErrorsAreDeliveredThroughFuture) {
  FfiClient::instance().shutdown();
  ClockVideoSourceConfig config;
  config.resolution = {1280, 720};
  config.framerate_fps = 30;
  std::future<std::shared_ptr<CaptureSource>> future;
  EXPECT_NO_THROW(future = CaptureSource::create(config));
  ASSERT_TRUE(future.valid());
  EXPECT_THROW(static_cast<void>(future.get()), CaptureSourceError);
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST(CaptureSourceProtoTest, GstreamerCanDiscoverResolution) {
  GstreamerVideoSourceConfig config;
  config.pipeline = "videotestsrc ! vp8enc ! appsink name=lk_appsink";
  const auto request = toProto(config);
  EXPECT_TRUE(request.IsInitialized());
  EXPECT_FALSE(request.gstreamer().has_resolution());
}

} // namespace
} // namespace livekit::test
