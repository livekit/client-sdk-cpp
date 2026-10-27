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

#include <google/protobuf/descriptor.h>
#include <google/protobuf/unknown_field_set.h>
#include <gtest/gtest.h>
#include <livekit/capture/capture_source.h>
#include <livekit/track.h>

#include <chrono>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>

#include "../common/ffi_utils.h"
#include "capture.pb.h"
#include "capture_source_internal.h"
#include "ffi_client.h"

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

using CreationOutcome = Result<std::shared_ptr<capture::CaptureSource>, capture::CaptureError>;
using JoinOutcome = Result<capture::CaptureResult, capture::CaptureError>;

template <typename T>
void expectError(const Result<T, capture::CaptureError>& result, capture::CaptureErrorKind kind) {
  ASSERT_TRUE(result.hasError());
  EXPECT_EQ(result.error().kind, kind);
  EXPECT_FALSE(result.error().message.empty());
}

template <typename Exception = std::runtime_error, typename Function>
void expectException(Function&& function, const std::string& diagnostic) {
  try {
    static_cast<void>(std::forward<Function>(function)());
    FAIL() << "Expected an exception containing: " << diagnostic;
  } catch (const Exception& error) {
    EXPECT_NE(std::string(error.what()).find(diagnostic), std::string::npos) << error.what();
  }
}

// Zero handles stand in for Rust resources: these tests exercise ownership of
// the C++ wrapper without starting a capture producer.
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

proto::FfiEvent joinFinishedEvent(std::uint64_t async_id, std::uint64_t frames_captured,
                                  proto::CaptureExit exit = proto::CAPTURE_EXIT_STOPPED) {
  proto::FfiEvent event;
  auto* callback = event.mutable_join_capture();
  callback->set_async_id(async_id);
  callback->mutable_finished()->set_frames_captured(frames_captured);
  callback->mutable_finished()->set_exit(exit);
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

  std::future<CreationOutcome> beginCreation() {
    return FfiClient::instance().newCaptureSourceAsync({}, [this](const proto::FfiRequest& request) {
      async_id_ = request.new_capture_source().request_async_id();
      proto::FfiResponse response;
      response.mutable_new_capture_source()->set_async_id(async_id_);
      return response;
    });
  }

  std::shared_ptr<capture::CaptureSource> completeCreation(const proto::OwnedCaptureSource& owned) {
    auto future = beginCreation();
    emitFfiEvent(creationEvent(async_id_, owned));
    return future.get().value();
  }

  std::future<JoinOutcome> beginJoin(std::uint64_t capture_handle = 42) {
    return FfiClient::instance().joinCaptureAsync(capture_handle,
                                                  [this, capture_handle](const proto::FfiRequest& request) {
                                                    EXPECT_EQ(request.join_capture().capture_handle(), capture_handle);
                                                    async_id_ = request.join_capture().request_async_id();
                                                    proto::FfiResponse response;
                                                    response.mutable_join_capture()->set_async_id(async_id_);
                                                    return response;
                                                  });
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
  EXPECT_EQ(source->kind(), capture::CaptureSourceKind::Pixel);
  EXPECT_EQ(source->width(), 1280);
  EXPECT_EQ(source->height(), 720);
  EXPECT_EQ(FfiClientTestAccess::listenerCount(), 0u);
  const std::weak_ptr<capture::CaptureSource> weak_source = source;
  const std::weak_ptr<VideoSource> weak_video = source->videoSource();
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
      expectException([&future] { return future.get(); }, "CaptureSource::create: invalid capture source info");
    }
    // A second drop must fail. If ownership leaked, this also cleans it up.
    EXPECT_FALSE(dropFfiHandle(capture.handle().id()));
    EXPECT_FALSE(dropFfiHandle(video.handle().id()));
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
  EXPECT_NE(future.get().value(), nullptr);
}

TEST_F(CaptureSourceCallbackTest, CreationStringErrorThrows) {
  auto future = beginCreation();
  proto::FfiEvent event;
  event.mutable_new_capture_source()->set_async_id(async_id_);
  event.mutable_new_capture_source()->set_error("discovery failed");
  emitFfiEvent(event);
  expectException([&future] { return future.get(); },
                  "CaptureSource::create: Rust source setup failed: discovery failed");
}

TEST_F(CaptureSourceCallbackTest, UnrelatedCallbackDoesNotCompleteFuture) {
  auto future = beginCreation();
  proto::FfiEvent event;
  event.mutable_new_capture_source()->set_async_id(async_id_ + 1);
  event.mutable_new_capture_source()->set_error("unrelated failure");
  emitFfiEvent(event);
  EXPECT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::timeout);
  emitFfiEvent(creationEvent(async_id_, makeOwned()));
  EXPECT_NE(future.get().value(), nullptr);
}

TEST_F(CaptureSourceCallbackTest, MissingSourceThrows) {
  auto future = beginCreation();
  proto::FfiEvent event;
  event.mutable_new_capture_source()->set_async_id(async_id_);
  emitFfiEvent(event);
  expectException([&future] { return future.get(); }, "CaptureSource::create: callback is missing source or error");
}

TEST_F(CaptureSourceCallbackTest, ShutdownCancelsPendingFutureWithException) {
  auto future = beginCreation();
  FfiClient::instance().shutdown();
  EXPECT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
  expectException([&future] { return future.get(); }, "CaptureSource::create: operation cancelled during SDK shutdown");
}

TEST_F(CaptureSourceCallbackTest, SendFailureRemovesPendingOperation) {
  auto future = FfiClient::instance().newCaptureSourceAsync(
      {}, [](const proto::FfiRequest&) -> proto::FfiResponse { throw std::runtime_error("send failed"); });
  expectException([&future] { return future.get(); }, "CaptureSource::create: send failed");
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, JoinCallbackMapsFinishedResult) {
  auto future = beginJoin();
  emitFfiEvent(joinFinishedEvent(async_id_, 123, proto::CAPTURE_EXIT_END_OF_STREAM));

  const auto result = future.get();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().frames_captured, 123u);
  EXPECT_EQ(result.value().exit, capture::CaptureExit::EndOfStream);
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, JoinCallbackMayCompleteBeforeSendReturns) {
  auto future = FfiClient::instance().joinCaptureAsync(42, [](const proto::FfiRequest& request) {
    const auto async_id = request.join_capture().request_async_id();
    emitFfiEvent(joinFinishedEvent(async_id, 3));
    proto::FfiResponse response;
    response.mutable_join_capture()->set_async_id(async_id);
    return response;
  });
  ASSERT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
  const auto result = future.get();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().frames_captured, 3u);
  EXPECT_EQ(result.value().exit, capture::CaptureExit::Stopped);
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, JoinIgnoresUnrelatedCallbacksAndDuplicateCompletion) {
  auto future = beginJoin();
  emitFfiEvent(joinFinishedEvent(async_id_ + 1, 99));
  proto::FfiEvent unrelated;
  unrelated.mutable_new_capture_source()->set_async_id(async_id_);
  unrelated.mutable_new_capture_source()->set_error("unrelated creation failure");
  emitFfiEvent(unrelated);
  EXPECT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::timeout);
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 1u);

  emitFfiEvent(joinFinishedEvent(async_id_, 7));
  emitFfiEvent(joinFinishedEvent(async_id_, 99));
  ASSERT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
  EXPECT_EQ(future.get().value().frames_captured, 7u);
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, JoinsCompleteIndependentlyInReverseOrder) {
  auto first = beginJoin(42);
  const auto first_async_id = async_id_;
  auto second = beginJoin(43);
  emitFfiEvent(joinFinishedEvent(async_id_, 2));
  EXPECT_EQ(first.wait_for(std::chrono::seconds(0)), std::future_status::timeout);
  ASSERT_EQ(second.wait_for(std::chrono::seconds(0)), std::future_status::ready);
  EXPECT_EQ(second.get().value().frames_captured, 2u);
  emitFfiEvent(joinFinishedEvent(first_async_id, 1));
  ASSERT_EQ(first.wait_for(std::chrono::seconds(0)), std::future_status::ready);
  EXPECT_EQ(first.get().value().frames_captured, 1u);
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, JoinMissingTerminalResultThrows) {
  auto future = beginJoin();
  proto::FfiEvent event;
  event.mutable_join_capture()->set_async_id(async_id_);
  emitFfiEvent(event);
  ASSERT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
  expectException([&future] { return future.get(); },
                  "CaptureSource::join: terminal result is missing finished or error");
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, JoinCallbackMapsPumpError) {
  const auto verify_kind = [this](proto::CaptureErrorKind proto_kind, capture::CaptureErrorKind expected_kind) {
    auto future = beginJoin();
    proto::FfiEvent event;
    auto* callback = event.mutable_join_capture();
    callback->set_async_id(async_id_);
    callback->mutable_error()->set_error("capture failed");
    callback->mutable_error()->set_kind(proto_kind);
    emitFfiEvent(event);

    const auto result = future.get();
    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().message, "capture failed");
    EXPECT_EQ(result.error().kind, expected_kind);
  };

  verify_kind(proto::CAPTURE_ERROR_KIND_SOURCE, capture::CaptureErrorKind::Source);
  verify_kind(proto::CAPTURE_ERROR_KIND_RTC, capture::CaptureErrorKind::Rtc);
  verify_kind(proto::CAPTURE_ERROR_KIND_PANICKED, capture::CaptureErrorKind::Panicked);
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, UnknownWireEnumsThrowThroughFutureWithoutEscapingCallback) {
  for (const bool unknown_error : {false, true}) {
    auto future = beginJoin();
    proto::FfiEvent event;
    auto* callback = event.mutable_join_capture();
    callback->set_async_id(async_id_);
    if (unknown_error) {
      auto* error = callback->mutable_error();
      error->set_error("future error kind");
      error->GetReflection()->MutableUnknownFields(error)->AddVarint(2, 99);
    } else {
      auto* finished = callback->mutable_finished();
      finished->set_frames_captured(1);
      finished->GetReflection()->MutableUnknownFields(finished)->AddVarint(2, 99);
    }
    // Proto2 retains unknown enum numbers as unknown fields; the required enum
    // field stays unset. Send the partial wire payload to exercise that case.
    std::string bytes;
    ASSERT_TRUE(event.SerializePartialToString(&bytes));
    EXPECT_NO_THROW(ffiEventCallback(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
    ASSERT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    expectException([&future] { return future.get(); }, "CaptureSource::join: invalid terminal result");
    EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
  }
}

TEST_F(CaptureSourceCallbackTest, ShutdownCancelsPendingJoinWithException) {
  auto future = beginJoin();
  FfiClient::instance().shutdown();
  EXPECT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
  expectException([&future] { return future.get(); }, "CaptureSource::join: operation cancelled during SDK shutdown");
}

TEST_F(CaptureSourceCallbackTest, JoinSendFailureRemovesPendingOperation) {
  auto future = FfiClient::instance().joinCaptureAsync(
      42, [](const proto::FfiRequest&) -> proto::FfiResponse { throw std::runtime_error("send failed"); });
  expectException([&future] { return future.get(); }, "CaptureSource::join: send failed");
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, JoinMissingAcknowledgementRemovesPendingOperation) {
  auto future =
      FfiClient::instance().joinCaptureAsync(42, [](const proto::FfiRequest&) { return proto::FfiResponse{}; });
  expectException([&future] { return future.get(); }, "CaptureSource::join: invalid join_capture acknowledgement");
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, JoinInvalidAcknowledgementRemovesPendingOperation) {
  for (const bool missing_id : {false, true}) {
    const auto send = [missing_id](const proto::FfiRequest& request) {
      proto::FfiResponse response;
      auto* acknowledgement = response.mutable_join_capture();
      if (!missing_id) acknowledgement->set_async_id(request.join_capture().request_async_id() + 1);
      return response;
    };
    auto future = FfiClient::instance().joinCaptureAsync(42, send);
    expectException([&future] { return future.get(); }, "CaptureSource::join: invalid join_capture acknowledgement");
    EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
  }
}

TEST_F(CaptureSourceCallbackTest, JoinApiReportsSynchronousSendFailureThroughFuture) {
  auto source = completeCreation(makeOwned());
  FfiClient::instance().shutdown();

  std::future<JoinOutcome> future;
  EXPECT_NO_THROW(future = source->join());
  EXPECT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
  expectException([&future] { return future.get(); }, "CaptureSource::join: LiveKit is not initialized");
  expectException([&source] { return source->start(); }, "CaptureSource::start: LiveKit is not initialized");
  expectException([&source] { return source->stop(); }, "CaptureSource::stop: LiveKit is not initialized");
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, ControlsReturnSuccessAndThrowUntypedBackendErrors) {
  for (const bool start : {false, true}) {
    for (const bool rejected : {false, true}) {
      const auto send = [start, rejected](const proto::FfiRequest& request) {
        EXPECT_EQ(start ? request.start_capture().capture_handle() : request.stop_capture().capture_handle(), 42u);
        proto::FfiResponse response;
        if (start) {
          auto* acknowledgement = response.mutable_start_capture();
          if (rejected) acknowledgement->set_error("opaque backend diagnostic");
        } else {
          auto* acknowledgement = response.mutable_stop_capture();
          if (rejected) acknowledgement->set_error("opaque backend diagnostic");
        }
        return response;
      };
      const auto control = [&] { return FfiClient::instance().controlCapture(42, start, send); };
      if (rejected) {
        const std::string operation = start ? "CaptureSource::start: " : "CaptureSource::stop: ";
        expectException(control, operation + "request rejected by Rust: opaque backend diagnostic");
      } else {
        EXPECT_TRUE(control().ok());
      }
    }
  }
}

TEST_F(CaptureSourceCallbackTest, ControlsThrowProtocolAndTransportFailures) {
  for (const bool start : {false, true}) {
    const std::string operation = start ? "CaptureSource::start: " : "CaptureSource::stop: ";
    expectException(
        [&] {
          return FfiClient::instance().controlCapture(42, start,
                                                      [](const proto::FfiRequest&) { return proto::FfiResponse{}; });
        },
        operation + "missing capture control acknowledgement");
    expectException(
        [&] {
          return FfiClient::instance().controlCapture(42, start, [](const proto::FfiRequest&) -> proto::FfiResponse {
            throw std::runtime_error("transport unavailable");
          });
        },
        operation + "transport unavailable");
  }
}

TEST_F(CaptureSourceCallbackTest, CreationRejectsInvalidAcknowledgements) {
  for (const bool missing_id : {false, true}) {
    auto future = FfiClient::instance().newCaptureSourceAsync({}, [missing_id](const proto::FfiRequest& request) {
      proto::FfiResponse response;
      auto* acknowledgement = response.mutable_new_capture_source();
      if (!missing_id) acknowledgement->set_async_id(request.new_capture_source().request_async_id() + 1);
      return response;
    });
    expectException([&future] { return future.get(); },
                    "CaptureSource::create: invalid new_capture_source acknowledgement");
    EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
  }
  auto future =
      FfiClient::instance().newCaptureSourceAsync({}, [](const proto::FfiRequest&) { return proto::FfiResponse{}; });
  expectException([&future] { return future.get(); },
                  "CaptureSource::create: invalid new_capture_source acknowledgement");
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, AllocationFailuresRemainExceptionalAndRemovePendingOperations) {
  const auto send = [](const proto::FfiRequest&) -> proto::FfiResponse { throw std::bad_alloc(); };
  EXPECT_THROW(static_cast<void>(FfiClient::instance().newCaptureSourceAsync({}, send)), std::bad_alloc);
  EXPECT_THROW(static_cast<void>(FfiClient::instance().joinCaptureAsync(42, send)), std::bad_alloc);
  EXPECT_THROW(static_cast<void>(FfiClient::instance().controlCapture(42, true, send)), std::bad_alloc);
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, UninitializedOperationsDoNotSendRequests) {
  FfiClient::instance().shutdown();
  bool sent = false;
  const auto send = [&sent](const proto::FfiRequest&) {
    sent = true;
    return proto::FfiResponse{};
  };
  auto created = FfiClient::instance().newCaptureSourceAsync({}, send);
  auto joined = FfiClient::instance().joinCaptureAsync(42, send);
  expectException([&created] { return created.get(); }, "CaptureSource::create: LiveKit is not initialized");
  expectException([&joined] { return joined.get(); }, "CaptureSource::join: LiveKit is not initialized");
  expectException([&] { return FfiClient::instance().controlCapture(42, true, send); },
                  "CaptureSource::start: LiveKit is not initialized");
  expectException([&] { return FfiClient::instance().controlCapture(42, false, send); },
                  "CaptureSource::stop: LiveKit is not initialized");
  EXPECT_FALSE(sent);
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, ShutdownDuringSendThrowsWithContext) {
  const auto send = [](const proto::FfiRequest&) -> proto::FfiResponse {
    FfiClient::instance().shutdown();
    throw std::runtime_error("send interrupted by shutdown");
  };
  auto created = FfiClient::instance().newCaptureSourceAsync({}, send);
  expectException([&created] { return created.get(); }, "CaptureSource::create: send interrupted by shutdown");
  ASSERT_TRUE(FfiClient::instance().initialize(false));
  auto joined = FfiClient::instance().joinCaptureAsync(42, send);
  expectException([&joined] { return joined.get(); }, "CaptureSource::join: send interrupted by shutdown");
  ASSERT_TRUE(FfiClient::instance().initialize(false));
  expectException([&] { return FfiClient::instance().controlCapture(42, true, send); },
                  "CaptureSource::start: send interrupted by shutdown");
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST_F(CaptureSourceCallbackTest, RepeatedBindingFailuresReleasePendingOperations) {
  constexpr int kIterations = 256;
  const auto send = [](const proto::FfiRequest&) { return proto::FfiResponse{}; };
  const auto started = std::chrono::steady_clock::now();
  for (int index = 0; index < kIterations; ++index) {
    auto created = FfiClient::instance().newCaptureSourceAsync({}, send);
    auto joined = FfiClient::instance().joinCaptureAsync(42, send);
    expectException([&created] { return created.get(); }, "CaptureSource::create:");
    expectException([&joined] { return joined.get(); }, "CaptureSource::join:");
    expectException([&] { return FfiClient::instance().controlCapture(42, true, send); }, "CaptureSource::start:");
    ASSERT_EQ(FfiClientTestAccess::pendingCount(), 0u);
  }
  const auto elapsed = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started).count();
  RecordProperty("capture_exception_ns_per_operation", std::to_string(elapsed / (3 * kIterations)));
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
    const auto video_encoding = options.video_encoding.value_or(VideoEncodingOptions{});
    EXPECT_EQ(video_encoding.max_bitrate, 2000000u);
    EXPECT_EQ(video_encoding.max_framerate, 30);
    ASSERT_TRUE(options.audio_encoding.has_value());
    EXPECT_EQ(options.audio_encoding.value_or(AudioEncodingOptions{}).max_bitrate, 64000u);
    EXPECT_EQ(options.dtx, application.dtx);
    EXPECT_EQ(options.red, application.red);
    EXPECT_EQ(options.preconnect_buffer, application.preconnect_buffer);
    ASSERT_TRUE(options.frame_metadata_features.has_value());
    const auto metadata_features = options.frame_metadata_features.value_or(FrameMetadataFeatures{});
    EXPECT_TRUE(metadata_features.user_timestamp);
    EXPECT_TRUE(metadata_features.frame_id);
    EXPECT_TRUE(metadata_features.user_data);
    EXPECT_EQ(options.degradation_preference, application.degradation_preference);
  }
}

TEST(CaptureSourceProtoTest, ClockConfigurationUsesMainlineWireContract) {
  capture::ClockVideoSourceConfig config;
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

TEST(CaptureSourceProtoTest, ErrorKindsMatchProtoExactly) {
  const std::pair<capture::CaptureErrorKind, proto::CaptureErrorKind> kinds[] = {
      {capture::CaptureErrorKind::Source, proto::CAPTURE_ERROR_KIND_SOURCE},
      {capture::CaptureErrorKind::Rtc, proto::CAPTURE_ERROR_KIND_RTC},
      {capture::CaptureErrorKind::Panicked, proto::CAPTURE_ERROR_KIND_PANICKED},
  };
  ASSERT_EQ(proto::CaptureErrorKind_descriptor()->value_count(), 3)
      << "Update the C++ enum and mapping when the protocol adds an error kind";
  for (const auto& [cpp_kind, proto_kind] : kinds) {
    EXPECT_EQ(static_cast<int>(cpp_kind), static_cast<int>(proto_kind));
  }
}

TEST(CaptureSourceProtoTest, RejectsNonPositiveDimensionsBeforeUnsignedConversion) {
  for (const capture::CaptureResolution resolution :
       {capture::CaptureResolution{0, 720}, capture::CaptureResolution{-1, 720}, capture::CaptureResolution{1280, 0},
        capture::CaptureResolution{1280, -1}}) {
    capture::PatternVideoSourceConfig pattern;
    pattern.resolution = resolution;
    pattern.framerate_fps = 30;
    expectException<std::invalid_argument>([&pattern] { return toProto(pattern); }, "CaptureSource::create:");
    capture::ClockVideoSourceConfig clock;
    clock.resolution = resolution;
    clock.framerate_fps = 30;
    expectException<std::invalid_argument>([&clock] { return toProto(clock); }, "CaptureSource::create:");
    capture::GstreamerVideoSourceConfig gstreamer;
    gstreamer.resolution = resolution;
    expectException<std::invalid_argument>([&gstreamer] { return toProto(gstreamer); }, "CaptureSource::create:");
  }
}

TEST(CaptureSourceProtoTest, RejectsZeroFrameRate) {
  capture::PatternVideoSourceConfig pattern;
  pattern.resolution = {1280, 720};
  expectException<std::invalid_argument>([&pattern] { return toProto(pattern); }, "CaptureSource::create:");
  capture::ClockVideoSourceConfig clock;
  clock.resolution = {1280, 720};
  expectException<std::invalid_argument>([&clock] { return toProto(clock); }, "CaptureSource::create:");
}

// Deliberately exercise invalid enum inputs at the public API boundary.
// NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange)
TEST(CaptureSourceProtoTest, RejectsInvalidEnumValues) {
  const capture::PatternVideoSourceConfig pattern{{1280, 720}, 30, static_cast<capture::Pattern>(99)};
  expectException<std::invalid_argument>([&pattern] { return toProto(pattern); }, "CaptureSource::create:");
  capture::GstreamerVideoSourceConfig gstreamer;
  gstreamer.pipeline = "videotestsrc ! vp8enc ! appsink name=lk_appsink";
  gstreamer.codec = static_cast<VideoCodec>(99);
  expectException<std::invalid_argument>([&gstreamer] { return toProto(gstreamer); }, "CaptureSource::create:");
  gstreamer.codec.reset();
  gstreamer.rate_control =
      capture::GstreamerRateControl{"encoder", "bitrate", static_cast<capture::GstreamerBitrateUnit>(99)};
  expectException<std::invalid_argument>([&gstreamer] { return toProto(gstreamer); }, "CaptureSource::create:");
}
// NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)

TEST(CaptureSourceProtoTest, ValidationErrorsAreDeliveredThroughFuture) {
  std::future<CreationOutcome> future;
  EXPECT_NO_THROW(future = capture::CaptureSource::create(capture::ClockVideoSourceConfig{}));
  ASSERT_TRUE(future.valid());
  expectException<std::invalid_argument>([&future] { return future.get(); },
                                         "CaptureSource::create: resolution dimensions must be positive");
}

TEST(CaptureSourceProtoTest, SendErrorsAreDeliveredThroughFuture) {
  FfiClient::instance().shutdown();
  capture::ClockVideoSourceConfig config;
  config.resolution = {1280, 720};
  config.framerate_fps = 30;
  std::future<CreationOutcome> future;
  EXPECT_NO_THROW(future = capture::CaptureSource::create(config));
  ASSERT_TRUE(future.valid());
  expectException([&future] { return future.get(); }, "CaptureSource::create: LiveKit is not initialized");
  EXPECT_EQ(FfiClientTestAccess::pendingCount(), 0u);
}

TEST(CaptureSourceProtoTest, GstreamerCanDiscoverResolution) {
  capture::GstreamerVideoSourceConfig config;
  config.pipeline = "videotestsrc ! vp8enc ! appsink name=lk_appsink";
  const auto request = toProto(config);
  EXPECT_TRUE(request.IsInitialized());
  EXPECT_FALSE(request.gstreamer().has_resolution());
}

} // namespace
} // namespace livekit::test
