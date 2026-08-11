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

#include <optional>
#include <stdexcept>

#include "capture.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"
#include "room_proto_converter.h"

namespace livekit {

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

proto::DeviceFrameFormat toProto(DeviceFrameFormat format) {
  switch (format) {
    case DeviceFrameFormat::I420:
      return proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_I420;
    case DeviceFrameFormat::Nv12:
      return proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_NV12;
    case DeviceFrameFormat::Bgra:
      return proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_BGRA;
    case DeviceFrameFormat::Rgb24:
      return proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_RGB24;
    case DeviceFrameFormat::Bgr24:
      return proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_BGR24;
    case DeviceFrameFormat::Yuyv:
      return proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_YUYV;
    case DeviceFrameFormat::Uyvy:
      return proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_UYVY;
    case DeviceFrameFormat::Grey:
      return proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_GREY;
    case DeviceFrameFormat::Mjpeg:
      return proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_MJPEG;
  }
  return proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_I420;
}

DeviceFrameFormat fromProto(proto::DeviceFrameFormat format) {
  switch (format) {
    case proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_I420:
      return DeviceFrameFormat::I420;
    case proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_NV12:
      return DeviceFrameFormat::Nv12;
    case proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_BGRA:
      return DeviceFrameFormat::Bgra;
    case proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_RGB24:
      return DeviceFrameFormat::Rgb24;
    case proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_BGR24:
      return DeviceFrameFormat::Bgr24;
    case proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_YUYV:
      return DeviceFrameFormat::Yuyv;
    case proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_UYVY:
      return DeviceFrameFormat::Uyvy;
    case proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_GREY:
      return DeviceFrameFormat::Grey;
    case proto::DeviceFrameFormat::DEVICE_FRAME_FORMAT_MJPEG:
      return DeviceFrameFormat::Mjpeg;
  }
  return DeviceFrameFormat::I420;
}

void toProto(const DeviceFormat& format, proto::DeviceFormat* out) {
  validateResolution(format.resolution, "DeviceFormat::resolution");
  out->mutable_resolution()->set_width(format.resolution.width);
  out->mutable_resolution()->set_height(format.resolution.height);
  out->set_framerate_fps(format.framerate_fps);
  out->set_frame_format(toProto(format.frame_format));
}

DeviceFormat fromProto(const proto::DeviceFormat& format) {
  DeviceFormat out;
  out.resolution.width = static_cast<int>(format.resolution().width());
  out.resolution.height = static_cast<int>(format.resolution().height());
  out.framerate_fps = format.framerate_fps();
  out.frame_format = fromProto(format.frame_format());
  return out;
}

CaptureDeviceInfo fromProto(const proto::CaptureDeviceInfo& info) {
  CaptureDeviceInfo out;
  out.id = info.id();
  out.name = info.name();
  if (info.has_model_id()) {
    out.model_id = info.model_id();
  }
  if (info.has_manufacturer()) {
    out.manufacturer = info.manufacturer();
  }
  out.formats.reserve(static_cast<std::size_t>(info.formats_size()));
  for (const proto::DeviceFormat& format : info.formats()) {
    out.formats.push_back(fromProto(format));
  }
  out.formats_complete = info.formats_complete();
  return out;
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

std::future<std::shared_ptr<CaptureSource>> CaptureSource::create(PatternVideoSourceConfig config) {
  proto::NewCaptureSourceRequest request;
  auto* pattern = request.mutable_pattern();
  pattern->mutable_resolution()->set_width(config.resolution.width);
  pattern->mutable_resolution()->set_height(config.resolution.height);
  pattern->set_framerate_fps(config.framerate_fps);
  pattern->set_pattern(toProto(config.pattern));
  return createFromRequest(std::move(request));
}

DeviceFormatRequest DeviceFormatRequest::exact(DeviceFormat format) {
  DeviceFormatRequest request;
  request.kind_ = Kind::Exact;
  request.format_ = format;
  return request;
}

DeviceFormatRequest DeviceFormatRequest::closest(DeviceFormat format) {
  DeviceFormatRequest request;
  request.kind_ = Kind::Closest;
  request.format_ = format;
  return request;
}

DeviceFormatRequest DeviceFormatRequest::highestFramerate(HighestFramerateConstraint constraint) {
  DeviceFormatRequest request;
  request.kind_ = Kind::HighestFramerate;
  request.highest_framerate_ = std::move(constraint);
  return request;
}

DeviceFormatRequest DeviceFormatRequest::highestResolution(HighestResolutionConstraint constraint) {
  DeviceFormatRequest request;
  request.kind_ = Kind::HighestResolution;
  request.highest_resolution_ = std::move(constraint);
  return request;
}

DeviceSelector DeviceSelector::index(std::uint32_t device_index) {
  DeviceSelector selector;
  selector.kind_ = Kind::Index;
  selector.index_ = device_index;
  return selector;
}

DeviceSelector DeviceSelector::id(std::string device_id) {
  DeviceSelector selector;
  selector.kind_ = Kind::Id;
  selector.id_ = std::move(device_id);
  return selector;
}

std::future<std::shared_ptr<CaptureSource>> CaptureSource::create(DeviceVideoSourceConfig config) {
  proto::NewCaptureSourceRequest request;
  auto* device = request.mutable_device();

  switch (config.device.kind_) {
    case DeviceSelector::Kind::Default:
      // Leave the `device` oneof unset for the platform default.
      break;
    case DeviceSelector::Kind::Index:
      device->set_device_index(config.device.index_);
      break;
    case DeviceSelector::Kind::Id:
      device->set_device_id(config.device.id_);
      break;
  }

  // Leave `format` unset for the device default. An absent field and an empty
  // DeviceFormatRequest are equivalent -- the server collapses both to
  // DeviceFormatRequest::Default -- but omitting it keeps has_format()
  // meaningful. What is not interchangeable is an empty *inner* message:
  // highest_framerate{} and highest_resolution{} select a format, so they must
  // be emitted even with no constraints set.
  const DeviceFormatRequest& format = config.format;
  switch (format.kind_) {
    case DeviceFormatRequest::Kind::Default:
      break;
    case DeviceFormatRequest::Kind::Exact:
      toProto(format.format_, device->mutable_format()->mutable_exact());
      break;
    case DeviceFormatRequest::Kind::Closest:
      toProto(format.format_, device->mutable_format()->mutable_closest());
      break;
    case DeviceFormatRequest::Kind::HighestFramerate: {
      auto* highest = device->mutable_format()->mutable_highest_framerate();
      if (format.highest_framerate_.resolution) {
        validateResolution(*format.highest_framerate_.resolution, "HighestFramerateConstraint::resolution");
        highest->mutable_resolution()->set_width(format.highest_framerate_.resolution->width);
        highest->mutable_resolution()->set_height(format.highest_framerate_.resolution->height);
      }
      if (format.highest_framerate_.frame_format) {
        highest->set_frame_format(toProto(*format.highest_framerate_.frame_format));
      }
      break;
    }
    case DeviceFormatRequest::Kind::HighestResolution: {
      auto* highest = device->mutable_format()->mutable_highest_resolution();
      if (format.highest_resolution_.framerate_fps) {
        highest->set_framerate_fps(*format.highest_resolution_.framerate_fps);
      }
      if (format.highest_resolution_.frame_format) {
        highest->set_frame_format(toProto(*format.highest_resolution_.frame_format));
      }
      break;
    }
  }

  return createFromRequest(std::move(request));
}

std::future<std::vector<CaptureDeviceInfo>> CaptureSource::listDevices() {
  // Issuing the request can fail synchronously (an FFI library built without
  // the capture feature rejects it outright). Report that through the returned
  // future so every failure reaches the caller as a CaptureSourceError from
  // exactly one place, as documented.
  std::optional<std::future<proto::CaptureDeviceList>> devices;
  try {
    devices.emplace(FfiClient::instance().listCaptureDevicesAsync());
  } catch (const std::exception& e) {
    return readyCaptureError<std::vector<CaptureDeviceInfo>>(e);
  }

  // Map the FFI payload onto the public type once the callback resolves. A
  // helper thread keeps the returned future's wait semantics standard.
  return std::async(std::launch::async, [devices = std::move(*devices)]() mutable {
    try {
      const proto::CaptureDeviceList list = devices.get();
      std::vector<CaptureDeviceInfo> out;
      out.reserve(static_cast<std::size_t>(list.devices_size()));
      for (const proto::CaptureDeviceInfo& info : list.devices()) {
        out.push_back(fromProto(info));
      }
      return out;
    } catch (const CaptureSourceError&) {
      throw;
    } catch (const std::exception& e) {
      throw CaptureSourceError(e.what());
    }
  });
}

std::future<std::shared_ptr<CaptureSource>> CaptureSource::createFromRequest(proto::NewCaptureSourceRequest request) {
  // See listDevices(): a synchronous send failure is delivered through the
  // future rather than thrown from the factory.
  std::optional<std::future<proto::OwnedCaptureSource>> owned;
  try {
    owned.emplace(FfiClient::instance().newCaptureSourceAsync(std::move(request)));
  } catch (const std::exception& e) {
    return readyCaptureError<std::shared_ptr<CaptureSource>>(e);
  }

  // Map the FFI payload onto a wrapper once the callback resolves. A helper
  // thread keeps the returned future's wait semantics standard.
  return std::async(std::launch::async, [owned = std::move(*owned)]() mutable {
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
