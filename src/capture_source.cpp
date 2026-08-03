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

#include <stdexcept>

#include "capture.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"
#include "room_proto_converter.h"

namespace livekit {

namespace {

proto::GstreamerBitrateUnit toProto(GstreamerBitrateUnit unit) {
  switch (unit) {
    case GstreamerBitrateUnit::Bps:
      return proto::GstreamerBitrateUnit::GSTREAMER_BITRATE_UNIT_BPS;
    case GstreamerBitrateUnit::Kbps:
      return proto::GstreamerBitrateUnit::GSTREAMER_BITRATE_UNIT_KBPS;
  }
  return proto::GstreamerBitrateUnit::GSTREAMER_BITRATE_UNIT_BPS;
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
  return createFromRequest(std::move(request));
}

std::future<std::shared_ptr<CaptureSource>> CaptureSource::create(DemoVideoSourceConfig config) {
  proto::NewCaptureSourceRequest request;
  auto* demo = request.mutable_demo();
  demo->mutable_resolution()->set_width(config.resolution.width);
  demo->mutable_resolution()->set_height(config.resolution.height);
  demo->set_framerate_fps(config.framerate_fps);
  return createFromRequest(std::move(request));
}

std::future<std::shared_ptr<CaptureSource>> CaptureSource::createFromRequest(proto::NewCaptureSourceRequest request) {
  auto owned = FfiClient::instance().newCaptureSourceAsync(std::move(request));
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
  source->handle_ = FfiHandle(static_cast<uintptr_t>(owned.handle().id()));
  source->kind_ = info.kind() == proto::CaptureSourceKind::CAPTURE_SOURCE_ENCODED ? CaptureSourceKind::Encoded
                                                                                  : CaptureSourceKind::Pixel;
  source->width_ = static_cast<int>(info.resolution().width());
  source->height_ = static_cast<int>(info.resolution().height());
  if (info.has_codec()) {
    source->codec_ = static_cast<VideoCodec>(info.codec());
  }
  source->source_publish_options_ = fromProto(info.recommended_publish_options());
  source->video_source_ = std::shared_ptr<VideoSource>(new VideoSource(
      FfiHandle(static_cast<uintptr_t>(info.video_source().handle().id())), source->width_, source->height_));

  // The terminal CaptureSourceEvent is unsolicited (not async-id
  // correlated); observe it with a listener filtered by our handle. The
  // destructor removes the listener before releasing the handle, so no
  // callback can outlive the wrapper.
  CaptureSource* raw = source.get();
  const std::uint64_t capture_handle = source->handle_.get();
  source->listener_id_ = FfiClient::instance().addListener([raw, capture_handle](const proto::FfiEvent& event) {
    if (!event.has_capture_source_event() || event.capture_source_event().capture_handle() != capture_handle) {
      return;
    }
    const CaptureResult result = resultFromEvent(event.capture_source_event());
    FinishedCallback callback;
    {
      const std::scoped_lock lock(raw->callback_mutex_);
      callback = raw->on_finished_;
    }
    if (callback) {
      callback(result);
    }
  });

  return source;
}

TrackPublishOptions CaptureSource::publishOptions(TrackPublishOptions options) const {
  const TrackPublishOptions& dictated = source_publish_options_;
  const auto overlay = [](auto& target, const auto& source) {
    if (source.has_value()) {
      target = source;
    }
  };
  overlay(options.video_encoding, dictated.video_encoding);
  overlay(options.audio_encoding, dictated.audio_encoding);
  overlay(options.video_codec, dictated.video_codec);
  overlay(options.dtx, dictated.dtx);
  overlay(options.red, dictated.red);
  overlay(options.simulcast, dictated.simulcast);
  overlay(options.source, dictated.source);
  overlay(options.stream, dictated.stream);
  overlay(options.preconnect_buffer, dictated.preconnect_buffer);
  overlay(options.frame_metadata_features, dictated.frame_metadata_features);
  overlay(options.degradation_preference, dictated.degradation_preference);
  overlay(options.video_encoder, dictated.video_encoder);
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
  const std::scoped_lock lock(callback_mutex_);
  on_finished_ = std::move(callback);
}

void CaptureSource::start() {
  proto::FfiRequest req;
  req.mutable_start_capture()->set_capture_handle(handle_.get());

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_start_capture()) {
    throw CaptureSourceError("FfiResponse missing start_capture");
  }
  if (resp.start_capture().has_error()) {
    throw CaptureSourceError(resp.start_capture().error());
  }
}

void CaptureSource::stop() {
  proto::FfiRequest req;
  req.mutable_stop_capture()->set_capture_handle(handle_.get());

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_stop_capture()) {
    throw CaptureSourceError("FfiResponse missing stop_capture");
  }
  if (resp.stop_capture().has_error()) {
    throw CaptureSourceError(resp.stop_capture().error());
  }
}

} // namespace livekit
