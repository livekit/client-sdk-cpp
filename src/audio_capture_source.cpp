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

#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#include "capture.pb.h"
#include "ffi_client.h"
#include "livekit/capture/audio_source.h"
#include "livekit/ffi_handle.h"

namespace livekit {

struct AudioSourceAccess {
  static std::shared_ptr<AudioSource> adopt(FfiHandle&& handle, int sample_rate, int num_channels) {
    return std::shared_ptr<AudioSource>(new AudioSource(std::move(handle), sample_rate, num_channels, 0));
  }
};

} // namespace livekit

namespace livekit::capture {

struct GstreamerAudioCaptureSource::Impl {
  FfiHandle handle;
  AudioCaptureFormat format;
  std::shared_ptr<AudioSource> audio_source;
};

GstreamerAudioCaptureSource::GstreamerAudioCaptureSource() : impl_(std::make_unique<Impl>()) {}
GstreamerAudioCaptureSource::~GstreamerAudioCaptureSource() = default;

std::future<Result<std::shared_ptr<GstreamerAudioCaptureSource>, CaptureError>> GstreamerAudioCaptureSource::create(
    const GstreamerAudioSourceConfig& config) {
  proto::NewAudioCaptureSourceRequest request;
  auto* gstreamer = request.mutable_gstreamer();
  gstreamer->set_pipeline(config.pipeline);
  gstreamer->set_sample_rate(config.sample_rate);
  gstreamer->set_num_channels(config.num_channels);
  gstreamer->set_frame_duration_ms(config.frame_duration_ms);
  return FfiClient::instance().newAudioCaptureSourceAsync(std::move(request));
}

std::shared_ptr<GstreamerAudioCaptureSource> GstreamerAudioCaptureSource::fromOwned(
    const proto::OwnedAudioCaptureSource& owned) {
  FfiHandle capture_handle(static_cast<uintptr_t>(owned.handle().id()));
  FfiHandle audio_handle(static_cast<uintptr_t>(owned.info().audio_source().handle().id()));
  const auto& info = owned.info();
  constexpr auto kMaxInt = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
  if (!owned.IsInitialized() || info.sample_rate() == 0 || info.num_channels() == 0 ||
      info.samples_per_channel() == 0 || info.sample_rate() > kMaxInt || info.num_channels() > kMaxInt) {
    throw std::runtime_error("GstreamerAudioCaptureSource::create: invalid audio capture source info");
  }

  std::shared_ptr<GstreamerAudioCaptureSource> source(new GstreamerAudioCaptureSource());
  source->impl_->handle = std::move(capture_handle);
  source->impl_->format = {info.sample_rate(), info.num_channels(), info.samples_per_channel()};
  source->impl_->audio_source = AudioSourceAccess::adopt(std::move(audio_handle), static_cast<int>(info.sample_rate()),
                                                         static_cast<int>(info.num_channels()));
  return source;
}

AudioCaptureFormat GstreamerAudioCaptureSource::format() const noexcept { return impl_->format; }

std::shared_ptr<AudioSource> GstreamerAudioCaptureSource::audioSource() const noexcept { return impl_->audio_source; }

Result<void, CaptureError> GstreamerAudioCaptureSource::start() {
  return FfiClient::instance().controlCapture(impl_->handle.get(), true);
}

Result<void, CaptureError> GstreamerAudioCaptureSource::stop() {
  return FfiClient::instance().controlCapture(impl_->handle.get(), false);
}

std::future<Result<CaptureResult, CaptureError>> GstreamerAudioCaptureSource::join() {
  return FfiClient::instance().joinCaptureAsync(impl_->handle.get());
}

} // namespace livekit::capture
