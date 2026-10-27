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

#include "livekit/capture/capture_source.h"

#include <exception>
#include <limits>
#include <stdexcept>

#include "capture.pb.h"
#include "capture_source_internal.h"
#include "ffi.pb.h"
#include "ffi_client.h"

namespace livekit::capture {

struct CaptureSource::Impl {
  FfiHandle handle;
  CaptureSourceKind kind = CaptureSourceKind::Pixel;
  int width = 0;
  int height = 0;
  std::optional<VideoCodec> codec;
  std::shared_ptr<VideoSource> video_source;
};

CaptureSource::CaptureSource() : impl_(std::make_unique<Impl>()) {}

CaptureSourceKind CaptureSource::kind() const noexcept { return impl_->kind; }
int CaptureSource::width() const noexcept { return impl_->width; }
int CaptureSource::height() const noexcept { return impl_->height; }
std::optional<VideoCodec> CaptureSource::codec() const noexcept { return impl_->codec; }
std::shared_ptr<VideoSource> CaptureSource::videoSource() const noexcept { return impl_->video_source; }

namespace {

using CreationResult = Result<std::shared_ptr<CaptureSource>, CaptureError>;

template <typename Config>
std::future<CreationResult> createCaptureSource(const Config& config) {
  try {
    return FfiClient::instance().newCaptureSourceAsync(toProto(config));
  } catch (const std::invalid_argument&) {
    std::promise<CreationResult> promise;
    promise.set_exception(std::current_exception());
    return promise.get_future();
  }
}

void validateResolution(const CaptureResolution& resolution) {
  if (resolution.width <= 0 || resolution.height <= 0) {
    throw std::invalid_argument("CaptureSource::create: resolution dimensions must be positive");
  }
}

template <typename ProtoConfig, typename Config>
void copyPixelConfig(ProtoConfig& target, const Config& config) {
  validateResolution(config.resolution);
  if (config.framerate_fps == 0) {
    throw std::invalid_argument("CaptureSource::create: frame rate must be positive");
  }
  target.mutable_resolution()->set_width(config.resolution.width);
  target.mutable_resolution()->set_height(config.resolution.height);
  target.set_framerate_fps(config.framerate_fps);
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

} // namespace

std::future<Result<std::shared_ptr<CaptureSource>, CaptureError>> CaptureSource::create(
    const GstreamerVideoSourceConfig& config) {
  return createCaptureSource(config);
}

proto::NewCaptureSourceRequest toProto(const GstreamerVideoSourceConfig& config) {
  if (config.resolution) {
    validateResolution(*config.resolution);
  }
  if (config.pipeline.empty() || (config.codec && !proto::VideoCodec_IsValid(static_cast<int>(*config.codec))) ||
      (config.rate_control && (config.rate_control->element.empty() || config.rate_control->property.empty() ||
                               (config.rate_control->unit != GstreamerBitrateUnit::Bps &&
                                config.rate_control->unit != GstreamerBitrateUnit::Kbps)))) {
    throw std::invalid_argument("CaptureSource::create: invalid GStreamer pipeline, codec, or rate-control binding");
  }
  proto::NewCaptureSourceRequest request;
  auto* gstreamer = request.mutable_gstreamer();
  gstreamer->set_pipeline(config.pipeline);
  if (config.codec) {
    gstreamer->set_codec(static_cast<proto::VideoCodec>(*config.codec));
  }
  if (config.resolution) {
    gstreamer->mutable_resolution()->set_width(config.resolution->width);
    gstreamer->mutable_resolution()->set_height(config.resolution->height);
  }
  if (config.rate_control) {
    auto* rate_control = gstreamer->mutable_rate_control();
    rate_control->set_element(config.rate_control->element);
    rate_control->set_property(config.rate_control->property);
    rate_control->set_unit(toProto(config.rate_control->unit));
  }
  return request;
}

std::future<Result<std::shared_ptr<CaptureSource>, CaptureError>> CaptureSource::create(
    PatternVideoSourceConfig config) {
  return createCaptureSource(config);
}

proto::NewCaptureSourceRequest toProto(const PatternVideoSourceConfig& config) {
  proto::NewCaptureSourceRequest request;
  auto* pattern = request.mutable_pattern();
  copyPixelConfig(*pattern, config);
  if (config.pattern != Pattern::Gradient && config.pattern != Pattern::Logo) {
    throw std::invalid_argument("CaptureSource::create: invalid capture pattern");
  }
  pattern->set_pattern(toProto(config.pattern));
  return request;
}

std::future<Result<std::shared_ptr<CaptureSource>, CaptureError>> CaptureSource::create(ClockVideoSourceConfig config) {
  return createCaptureSource(config);
}

proto::NewCaptureSourceRequest toProto(const ClockVideoSourceConfig& config) {
  proto::NewCaptureSourceRequest request;
  copyPixelConfig(*request.mutable_clock(), config);
  return request;
}

std::shared_ptr<CaptureSource> CaptureSource::fromOwned(const proto::OwnedCaptureSource& owned) {
  const proto::CaptureSourceInfo& info = owned.info();
  // Guard both Rust-owned handles before validation or wrapper allocations.
  FfiHandle capture_handle_owner(static_cast<uintptr_t>(owned.handle().id()));
  FfiHandle video_handle_owner(static_cast<uintptr_t>(info.video_source().handle().id()));
  if (!owned.IsInitialized() || info.resolution().width() == 0 || info.resolution().height() == 0 ||
      info.resolution().width() > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
      info.resolution().height() > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
      (info.kind() == proto::CAPTURE_SOURCE_ENCODED && !info.has_codec())) {
    throw std::runtime_error("CaptureSource::create: invalid capture source info");
  }

  std::shared_ptr<CaptureSource> source(new CaptureSource());
  source->impl_->handle = std::move(capture_handle_owner);
  source->impl_->kind = info.kind() == proto::CaptureSourceKind::CAPTURE_SOURCE_ENCODED ? CaptureSourceKind::Encoded
                                                                                        : CaptureSourceKind::Pixel;
  source->impl_->width = static_cast<int>(info.resolution().width());
  source->impl_->height = static_cast<int>(info.resolution().height());
  if (info.has_codec()) {
    source->impl_->codec = static_cast<VideoCodec>(info.codec());
  }
  source->impl_->video_source =
      VideoSourceAccess::adopt(std::move(video_handle_owner), source->impl_->width, source->impl_->height);

  return source;
}

TrackPublishOptions CaptureSource::publishOptions(TrackPublishOptions options) const {
  if (impl_->kind == CaptureSourceKind::Encoded) {
    options.video_codec = impl_->codec;
    options.video_encoder = VideoEncoderBackend::PreEncoded;
    options.simulcast = false;
  }
  return options;
}

CaptureSource::~CaptureSource() = default;

Result<void, CaptureError> CaptureSource::start() {
  return FfiClient::instance().controlCapture(impl_->handle.get(), true);
}
Result<void, CaptureError> CaptureSource::stop() {
  return FfiClient::instance().controlCapture(impl_->handle.get(), false);
}
std::future<Result<CaptureResult, CaptureError>> CaptureSource::join() {
  return FfiClient::instance().joinCaptureAsync(impl_->handle.get());
}

} // namespace livekit::capture
