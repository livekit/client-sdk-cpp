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

#include <mutex>
#include <stdexcept>

#include "capture.pb.h"
#include "capture_source_internal.h"
#include "ffi.pb.h"
#include "ffi_client.h"
#include "lk_log.h"
#include "room_proto_converter.h"

namespace livekit {

struct CaptureSource::Impl {
  FfiHandle handle;
  CaptureSourceKind kind = CaptureSourceKind::Pixel;
  int width = 0;
  int height = 0;
  std::optional<VideoCodec> codec;
  std::shared_ptr<VideoSource> video_source;
  TrackPublishOptions source_publish_options;
  std::mutex callback_mutex;
  FinishedCallback on_finished;
  int listener_id = 0;
};

CaptureSource::CaptureSource() : impl_(std::make_unique<Impl>()) {}

CaptureSourceKind CaptureSource::kind() const noexcept { return impl_->kind; }
int CaptureSource::width() const noexcept { return impl_->width; }
int CaptureSource::height() const noexcept { return impl_->height; }
std::optional<VideoCodec> CaptureSource::codec() const noexcept { return impl_->codec; }
std::shared_ptr<VideoSource> CaptureSource::videoSource() const noexcept { return impl_->video_source; }

namespace {

/// A already-failed future carrying @p error as a @ref CaptureSourceError.
///
/// Lets the factories report a synchronous send failure through the future
/// they return, keeping one error channel for every failure mode.
template <typename T>
std::future<T> readyCaptureError(const std::exception& error) {
  std::promise<T> promise;
  promise.set_exception(std::make_exception_ptr(CaptureSourceError(error.what())));
  return promise.get_future();
}

void validateResolution(const CaptureResolution& resolution) {
  if (resolution.width <= 0 || resolution.height <= 0) {
    throw CaptureSourceError("Capture resolution dimensions must be positive");
  }
}

void validatePixelConfig(const CaptureResolution& resolution, std::uint32_t framerate_fps) {
  validateResolution(resolution);
  if (framerate_fps == 0) {
    throw CaptureSourceError("Capture frame rate must be positive");
  }
}

proto::GstreamerBitrateUnit toProto(GstreamerBitrateUnit unit) {
  switch (unit) {
    case GstreamerBitrateUnit::Bps:
      return proto::GstreamerBitrateUnit::GSTREAMER_BITRATE_UNIT_BPS;
    case GstreamerBitrateUnit::Kbps:
      return proto::GstreamerBitrateUnit::GSTREAMER_BITRATE_UNIT_KBPS;
  }
  return proto::GstreamerBitrateUnit::GSTREAMER_BITRATE_UNIT_BPS;
}

proto::Pattern toProto(Pattern pattern) {
  switch (pattern) {
    case Pattern::Gradient:
      return proto::Pattern::PATTERN_GRADIENT;
    case Pattern::Logo:
      return proto::Pattern::PATTERN_LOGO;
  }
  return proto::Pattern::PATTERN_GRADIENT;
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
  }
  return result;
}

} // namespace

std::future<std::shared_ptr<CaptureSource>> CaptureSource::create(GstreamerVideoSourceConfig config) {
  try {
    return createFromRequest(toProto(std::move(config)));
  } catch (const CaptureSourceError& error) {
    return readyCaptureError<std::shared_ptr<CaptureSource>>(error);
  }
}

proto::NewCaptureSourceRequest toProto(GstreamerVideoSourceConfig config) {
  if (config.resolution) {
    validateResolution(*config.resolution);
  }
  proto::NewCaptureSourceRequest request;
  auto* gstreamer = request.mutable_gstreamer();
  gstreamer->set_pipeline(std::move(config.pipeline));
  if (config.codec) {
    gstreamer->set_codec(static_cast<proto::VideoCodec>(*config.codec));
  }
  if (config.resolution) {
    gstreamer->mutable_resolution()->set_width(config.resolution->width);
    gstreamer->mutable_resolution()->set_height(config.resolution->height);
  }
  if (config.rate_control) {
    auto* rate_control = gstreamer->mutable_rate_control();
    rate_control->set_element(std::move(config.rate_control->element));
    rate_control->set_property(std::move(config.rate_control->property));
    rate_control->set_unit(toProto(config.rate_control->unit));
  }
  return request;
}

std::future<std::shared_ptr<CaptureSource>> CaptureSource::create(PatternVideoSourceConfig config) {
  try {
    return createFromRequest(toProto(config));
  } catch (const CaptureSourceError& error) {
    return readyCaptureError<std::shared_ptr<CaptureSource>>(error);
  }
}

proto::NewCaptureSourceRequest toProto(const PatternVideoSourceConfig& config) {
  validatePixelConfig(config.resolution, config.framerate_fps);
  proto::NewCaptureSourceRequest request;
  auto* pattern = request.mutable_pattern();
  pattern->mutable_resolution()->set_width(config.resolution.width);
  pattern->mutable_resolution()->set_height(config.resolution.height);
  pattern->set_framerate_fps(config.framerate_fps);
  pattern->set_pattern(toProto(config.pattern));
  return request;
}

std::future<std::shared_ptr<CaptureSource>> CaptureSource::create(ClockVideoSourceConfig config) {
  try {
    return createFromRequest(toProto(config));
  } catch (const CaptureSourceError& error) {
    return readyCaptureError<std::shared_ptr<CaptureSource>>(error);
  }
}

proto::NewCaptureSourceRequest toProto(const ClockVideoSourceConfig& config) {
  validatePixelConfig(config.resolution, config.framerate_fps);
  proto::NewCaptureSourceRequest request;
  auto* clock = request.mutable_clock();
  clock->mutable_resolution()->set_width(config.resolution.width);
  clock->mutable_resolution()->set_height(config.resolution.height);
  clock->set_framerate_fps(config.framerate_fps);
  return request;
}

std::future<std::shared_ptr<CaptureSource>> CaptureSource::createFromRequest(proto::NewCaptureSourceRequest request) {
  // A synchronous send failure is delivered through the
  // future rather than thrown from the factory.
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

  std::shared_ptr<CaptureSource> source(new CaptureSource());
  source->impl_->handle = FfiHandle(static_cast<uintptr_t>(owned.handle().id()));
  source->impl_->kind = info.kind() == proto::CaptureSourceKind::CAPTURE_SOURCE_ENCODED ? CaptureSourceKind::Encoded
                                                                                        : CaptureSourceKind::Pixel;
  source->impl_->width = static_cast<int>(info.resolution().width());
  source->impl_->height = static_cast<int>(info.resolution().height());
  if (info.has_codec()) {
    source->impl_->codec = static_cast<VideoCodec>(info.codec());
  }
  source->impl_->source_publish_options = fromProto(info.recommended_publish_options());
  source->impl_->video_source =
      std::shared_ptr<VideoSource>(new VideoSource(FfiHandle(static_cast<uintptr_t>(info.video_source().handle().id())),
                                                   source->impl_->width, source->impl_->height));

  // The terminal CaptureSourceEvent is unsolicited (not async-id
  // correlated); observe it with a listener filtered by our handle. The
  // destructor removes the listener before releasing the handle, so no
  // callback can outlive the wrapper.
  CaptureSource* raw = source.get();
  const std::uint64_t capture_handle = source->impl_->handle.get();
  source->impl_->listener_id = FfiClient::instance().addListener([raw, capture_handle](const proto::FfiEvent& event) {
    if (!event.has_capture_source_event() || event.capture_source_event().capture_handle() != capture_handle) {
      return;
    }
    const CaptureResult result = resultFromEvent(event.capture_source_event());
    FinishedCallback callback;
    {
      const std::scoped_lock lock(raw->impl_->callback_mutex);
      callback = raw->impl_->on_finished;
    }
    if (callback) {
      callback(result);
    }
  });

  return source;
}

TrackPublishOptions CaptureSource::publishOptions(TrackPublishOptions options) const {
  const auto overlay = [](auto& target, const auto& source) {
    if (source.has_value()) {
      target = source;
    }
  };
  overlay(options.video_encoding, impl_->source_publish_options.video_encoding);
  overlay(options.audio_encoding, impl_->source_publish_options.audio_encoding);
  overlay(options.video_codec, impl_->source_publish_options.video_codec);
  overlay(options.dtx, impl_->source_publish_options.dtx);
  overlay(options.red, impl_->source_publish_options.red);
  overlay(options.simulcast, impl_->source_publish_options.simulcast);
  overlay(options.source, impl_->source_publish_options.source);
  overlay(options.stream, impl_->source_publish_options.stream);
  overlay(options.preconnect_buffer, impl_->source_publish_options.preconnect_buffer);
  overlay(options.frame_metadata_features, impl_->source_publish_options.frame_metadata_features);
  overlay(options.degradation_preference, impl_->source_publish_options.degradation_preference);
  overlay(options.video_encoder, impl_->source_publish_options.video_encoder);
  return options;
}

CaptureSource::~CaptureSource() {
  if (impl_->listener_id != 0) {
    FfiClient::instance().removeListener(impl_->listener_id);
    impl_->listener_id = 0;
  }
  // Dropping the handle stops a running capture.
}

void CaptureSource::setOnFinishedCallback(FinishedCallback callback) {
  const std::scoped_lock lock(impl_->callback_mutex);
  impl_->on_finished = std::move(callback);
}

bool CaptureSource::start() {
  try {
    proto::FfiRequest req;
    req.mutable_start_capture()->set_capture_handle(impl_->handle.get());

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
    req.mutable_stop_capture()->set_capture_handle(impl_->handle.get());

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
