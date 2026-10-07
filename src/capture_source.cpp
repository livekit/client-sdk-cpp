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

#include "livekit/capture_source.h"

#include <limits>
#include <stdexcept>
#include <utility>

#include "capture.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"
#include "lk_log.h"
#include "room_proto_converter.h"

namespace livekit {

namespace {

/// A failed future carrying @p error as a @ref CaptureSourceError.
///
/// Lets the factories report a synchronous send failure through the future
/// they return, keeping one error channel for every failure mode.
template <typename T>
std::future<T> readyCaptureError(const std::exception& error) {
  std::promise<T> promise;
  promise.set_exception(std::make_exception_ptr(CaptureSourceError(error.what())));
  return promise.get_future();
}

/// Rejects a resolution that cannot be represented on the wire.
///
/// @ref CaptureResolution is signed while the protocol carries unsigned
/// dimensions, so a negative value would otherwise be reinterpreted as a
/// dimension near 2^32.
void validateResolution(const CaptureResolution& resolution, const char* field) {
  if (resolution.width <= 0 || resolution.height <= 0) {
    throw CaptureSourceError(std::string(field) + " must be positive, got " + std::to_string(resolution.width) + "x" +
                             std::to_string(resolution.height));
  }
}

proto::Pattern toProto(Pattern pattern) {
  switch (pattern) {
    case Pattern::Gradient:
      return proto::Pattern::PATTERN_GRADIENT;
    case Pattern::Logo:
      return proto::Pattern::PATTERN_LOGO;
  }
  throw CaptureSourceError("unsupported capture pattern");
}

CaptureResult resultFromEvent(const proto::CaptureSourceEvent& event) {
  CaptureResult result;
  if (event.has_error()) {
    result.error = event.error().error();
    return result;
  }
  if (event.has_finished()) {
    result.frames_captured = event.finished().frames_captured();
    result.exit = event.finished().exit() == proto::CaptureExit::CAPTURE_EXIT_END_OF_STREAM ? CaptureExit::EndOfStream
                                                                                            : CaptureExit::Stopped;
  } else {
    result.error = "capture finished event has no result";
  }
  return result;
}

} // namespace

std::future<std::shared_ptr<CaptureSource>> CaptureSource::create(PatternVideoSourceConfig config) {
  try {
    validateResolution(config.resolution, "PatternVideoSourceConfig::resolution");
    if (config.framerate_fps == 0) {
      throw CaptureSourceError("PatternVideoSourceConfig::framerate_fps must be positive");
    }
  } catch (const std::exception& error) {
    return readyCaptureError<std::shared_ptr<CaptureSource>>(error);
  }

  proto::NewCaptureSourceRequest request;
  auto* pattern = request.mutable_pattern();
  pattern->mutable_resolution()->set_width(config.resolution.width);
  pattern->mutable_resolution()->set_height(config.resolution.height);
  pattern->set_framerate_fps(config.framerate_fps);
  try {
    pattern->set_pattern(toProto(config.pattern));
  } catch (const std::exception& error) {
    return readyCaptureError<std::shared_ptr<CaptureSource>>(error);
  }
  return createFromRequest(std::move(request));
}

std::future<std::shared_ptr<CaptureSource>> CaptureSource::create(ClockVideoSourceConfig config) {
  try {
    validateResolution(config.resolution, "ClockVideoSourceConfig::resolution");
    if (config.framerate_fps == 0) {
      throw CaptureSourceError("ClockVideoSourceConfig::framerate_fps must be positive");
    }
  } catch (const std::exception& error) {
    return readyCaptureError<std::shared_ptr<CaptureSource>>(error);
  }

  proto::NewCaptureSourceRequest request;
  auto* clock = request.mutable_clock();
  clock->mutable_resolution()->set_width(config.resolution.width);
  clock->mutable_resolution()->set_height(config.resolution.height);
  clock->set_framerate_fps(config.framerate_fps);
  return createFromRequest(std::move(request));
}

std::future<std::shared_ptr<CaptureSource>> CaptureSource::createFromRequest(proto::NewCaptureSourceRequest request) {
  // A synchronous send failure is delivered through the returned future.
  std::future<proto::OwnedCaptureSource> owned;
  try {
    owned = FfiClient::instance().newCaptureSourceAsync(std::move(request));
  } catch (const std::exception& e) {
    return readyCaptureError<std::shared_ptr<CaptureSource>>(e);
  }

  // Map the FFI payload onto a wrapper once the callback resolves. A helper
  // thread keeps the returned future's wait semantics standard.
  return std::async(std::launch::async, [owned = std::move(owned)]() mutable {
    try {
      return fromOwned(owned.get());
    } catch (const CaptureSourceError&) {
      throw;
    } catch (const std::exception& e) {
      throw CaptureSourceError(e.what());
    }
  });
}

std::shared_ptr<CaptureSource> CaptureSource::fromOwned(const proto::OwnedCaptureSource& owned) {
  const proto::CaptureSourceInfo& info = owned.info();
  FfiHandle capture_handle(static_cast<uintptr_t>(owned.handle().id()));
  FfiHandle video_handle(static_cast<uintptr_t>(info.video_source().handle().id()));
  if (!capture_handle || !video_handle) {
    throw CaptureSourceError("capture source returned an invalid handle");
  }
  if (info.resolution().width() == 0 || info.resolution().height() == 0 ||
      info.resolution().width() > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
      info.resolution().height() > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    throw CaptureSourceError("capture source returned an invalid resolution");
  }
  if (info.kind() != proto::CaptureSourceKind::CAPTURE_SOURCE_PIXEL &&
      info.kind() != proto::CaptureSourceKind::CAPTURE_SOURCE_ENCODED) {
    throw CaptureSourceError("capture source returned an invalid kind");
  }

  std::shared_ptr<CaptureSource> source(new CaptureSource());
  source->handle_ = std::move(capture_handle);
  source->kind_ = info.kind() == proto::CaptureSourceKind::CAPTURE_SOURCE_ENCODED ? CaptureSourceKind::Encoded
                                                                                  : CaptureSourceKind::Pixel;
  source->width_ = static_cast<int>(info.resolution().width());
  source->height_ = static_cast<int>(info.resolution().height());
  if (info.has_codec()) {
    source->codec_ = static_cast<VideoCodec>(info.codec());
  }
  source->source_publish_options_ = fromProto(info.recommended_publish_options());
  source->video_source_ =
      std::shared_ptr<VideoSource>(new VideoSource(std::move(video_handle), source->width_, source->height_));

  // The terminal CaptureSourceEvent is unsolicited (not async-id
  // correlated); observe it with a listener filtered by our handle. The
  // destructor removes the listener before releasing the handle, so no
  // callback can outlive the wrapper.
  CaptureSource* raw = source.get();
  const std::uint64_t capture_handle_id = source->handle_.get();
  source->listener_id_ = FfiClient::instance().addListener([raw, capture_handle_id](const proto::FfiEvent& event) {
    if (!event.has_capture_source_event() || event.capture_source_event().capture_handle() != capture_handle_id) {
      return;
    }
    const CaptureResult result = resultFromEvent(event.capture_source_event());
    std::shared_ptr<const FinishedCallback> callback;
    {
      const std::scoped_lock lock(raw->callback_mutex_);
      callback = raw->on_finished_;
    }
    if (callback) {
      (*callback)(result);
    }
  });

  return source;
}

TrackPublishOptions CaptureSource::publishOptions(TrackPublishOptions options) const {
  // The Rust FFI currently recommends codec, encoder backend, and simulcast.
  // Pixel sources may use application choices; encoded sources require their
  // passthrough settings to override them.
  if (kind_ == CaptureSourceKind::Encoded || !options.video_codec) {
    options.video_codec = source_publish_options_.video_codec;
  }
  if (kind_ == CaptureSourceKind::Encoded || !options.video_encoder) {
    options.video_encoder = source_publish_options_.video_encoder;
  }
  if (kind_ == CaptureSourceKind::Encoded || !options.simulcast) {
    options.simulcast = source_publish_options_.simulcast;
  }
  return options;
}

CaptureSource::~CaptureSource() {
  if (listener_id_ != 0) {
    FfiClient::instance().removeListener(listener_id_);
    listener_id_ = 0;
  }
  // Dropping the handle stops a running capture.
}

void CaptureSource::setOnFinishedCallback(FinishedCallback callback) {
  std::shared_ptr<const FinishedCallback> next;
  if (callback) {
    next = std::make_shared<FinishedCallback>(std::move(callback));
  }
  const std::scoped_lock lock(callback_mutex_);
  on_finished_ = std::move(next);
}

bool CaptureSource::start() {
  try {
    proto::FfiRequest req;
    req.mutable_start_capture()->set_capture_handle(handle_.get());

    const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
    if (!resp.has_start_capture()) {
      LK_LOG_WARN("Capture start request returned no start_capture response");
      return false;
    }
    if (resp.start_capture().has_error()) {
      LK_LOG_WARN("Capture start request failed: {}", resp.start_capture().error());
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    LK_LOG_WARN("Capture start request failed: {}", e.what());
    return false;
  }
}

bool CaptureSource::stop() {
  try {
    proto::FfiRequest req;
    req.mutable_stop_capture()->set_capture_handle(handle_.get());

    const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
    if (!resp.has_stop_capture()) {
      LK_LOG_WARN("Capture stop request returned no stop_capture response");
      return false;
    }
    if (resp.stop_capture().has_error()) {
      LK_LOG_WARN("Capture stop request failed: {}", resp.stop_capture().error());
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    LK_LOG_WARN("Capture stop request failed: {}", e.what());
    return false;
  }
}

} // namespace livekit
