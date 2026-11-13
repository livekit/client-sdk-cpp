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
#include <livekit/capture/audio_source.h>
#include <livekit/livekit.h>

#include <chrono>
#include <string>

#include "../common/ffi_utils.h"
#include "capture.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"

namespace livekit::test {
namespace {

using namespace std::chrono_literals;

class GstreamerAudioTest : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(initialize(LogLevel::Warn)); }
  void TearDown() override { shutdown(); }
};

TEST_F(GstreamerAudioTest, CapturesSyntheticPcmAndReportsFormatAndStats) {
  capture::GstreamerAudioSourceConfig config;
  config.pipeline =
      "audiotestsrc wave=sine samplesperbuffer=480 num-buffers=8 ! "
      "audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=1 ! "
      "appsink name=lk_audio_appsink";

  auto pending = capture::GstreamerAudioCaptureSource::create(config);
  ASSERT_EQ(pending.wait_for(10s), std::future_status::ready);
  const auto created = pending.get();
  ASSERT_TRUE(created.ok()) << created.error().message;
  const auto source = created.value();
  ASSERT_NE(source, nullptr);
  EXPECT_EQ(source->format().sample_rate, 48000u);
  EXPECT_EQ(source->format().num_channels, 1u);
  EXPECT_EQ(source->format().samples_per_channel, 480u);
  ASSERT_NE(source->audioSource(), nullptr);
  EXPECT_EQ(source->audioSource()->sampleRate(), 48000);
  EXPECT_EQ(source->audioSource()->numChannels(), 1);

  ASSERT_TRUE(source->start().ok());
  auto joined = source->join();
  ASSERT_EQ(joined.wait_for(10s), std::future_status::ready);
  const auto finished = joined.get();
  ASSERT_TRUE(finished.ok()) << finished.error().message;
  EXPECT_EQ(finished.value().exit, capture::CaptureExit::EndOfStream);
  EXPECT_EQ(finished.value().frames_captured, 8u);
  EXPECT_EQ(finished.value().samples_captured, 3840u);
  EXPECT_EQ(finished.value().bytes_captured, 7680u);
  EXPECT_TRUE(finished.value().first_timestamp_ns.has_value());
  EXPECT_TRUE(finished.value().last_timestamp_ns.has_value());
}

TEST_F(GstreamerAudioTest, StartsEmptyPlayoutAndStopsWithZeroStats) {
  capture::GstreamerAudioPlayoutConfig config;
  config.pipeline =
      "appsrc name=lk_audio_appsrc ! "
      "audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=1 ! "
      "fakesink sync=false";

  const auto sink = capture::GstreamerAudioPlayoutSink::create(config);
  ASSERT_NE(sink, nullptr);
  auto detached = sink->detachTrack("not-attached");
  ASSERT_EQ(detached.wait_for(10s), std::future_status::ready);
  EXPECT_FALSE(detached.get());

  auto stopped = sink->stop();
  ASSERT_EQ(stopped.wait_for(10s), std::future_status::ready);
  const auto stats = stopped.get();
  EXPECT_EQ(stats.frames_pushed, 0u);
  EXPECT_EQ(stats.frames_dropped, 0u);
  EXPECT_EQ(stats.bytes_pushed, 0u);
}

TEST_F(GstreamerAudioTest, RepeatedEmptyPlayoutLifecycleRemainsBounded) {
  constexpr int kIterations = 16;
  capture::GstreamerAudioPlayoutConfig config;
  config.pipeline = "appsrc name=lk_audio_appsrc ! fakesink sync=false";
  const auto started = std::chrono::steady_clock::now();
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    const auto sink = capture::GstreamerAudioPlayoutSink::create(config);
    ASSERT_NE(sink, nullptr);
    ASSERT_EQ(sink->stop().get().frames_pushed, 0u);
  }
  const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
  RecordProperty("empty_playout_lifecycle_us_per_iteration", std::to_string(elapsed / kIterations));
}

TEST_F(GstreamerAudioTest, PlayoutCallbacksMapAttachDetachAndStopResults) {
  auto attached = FfiClient::instance().attachAudioPlayoutTrackAsync(41, 42, [](const proto::FfiRequest& request) {
    const auto async_id = request.attach_audio_playout_track().request_async_id();
    proto::FfiEvent event;
    auto* callback = event.mutable_attach_audio_playout_track();
    callback->set_async_id(async_id);
    callback->set_track_id("remote-track-id");
    emitFfiEvent(event);
    proto::FfiResponse response;
    response.mutable_attach_audio_playout_track()->set_async_id(async_id);
    return response;
  });
  EXPECT_EQ(attached.get(), "remote-track-id");

  auto detached =
      FfiClient::instance().detachAudioPlayoutTrackAsync(41, "remote-track-id", [](const proto::FfiRequest& request) {
        const auto async_id = request.detach_audio_playout_track().request_async_id();
        proto::FfiEvent event;
        auto* callback = event.mutable_detach_audio_playout_track();
        callback->set_async_id(async_id);
        callback->set_detached(true);
        emitFfiEvent(event);
        proto::FfiResponse response;
        response.mutable_detach_audio_playout_track()->set_async_id(async_id);
        return response;
      });
  EXPECT_TRUE(detached.get());

  auto stopped = FfiClient::instance().stopAudioPlayoutAsync(41, [](const proto::FfiRequest& request) {
    const auto async_id = request.stop_audio_playout().request_async_id();
    proto::FfiEvent event;
    auto* callback = event.mutable_stop_audio_playout();
    callback->set_async_id(async_id);
    callback->set_frames_pushed(10);
    callback->set_frames_dropped(2);
    callback->set_bytes_pushed(19200);
    emitFfiEvent(event);
    proto::FfiResponse response;
    response.mutable_stop_audio_playout()->set_async_id(async_id);
    return response;
  });
  const auto stats = stopped.get();
  EXPECT_EQ(stats.frames_pushed, 10u);
  EXPECT_EQ(stats.frames_dropped, 2u);
  EXPECT_EQ(stats.bytes_pushed, 19200u);
}

} // namespace
} // namespace livekit::test
