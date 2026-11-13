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

#include <utility>

#include "capture.pb.h"
#include "ffi_client.h"
#include "livekit/capture/audio_source.h"
#include "livekit/ffi_handle.h"

namespace livekit::capture {

struct GstreamerAudioPlayoutSink::Impl {
  FfiHandle handle;
};

GstreamerAudioPlayoutSink::GstreamerAudioPlayoutSink() : impl_(std::make_unique<Impl>()) {}
GstreamerAudioPlayoutSink::~GstreamerAudioPlayoutSink() = default;

std::shared_ptr<GstreamerAudioPlayoutSink> GstreamerAudioPlayoutSink::create(
    const GstreamerAudioPlayoutConfig& config) {
  proto::NewAudioPlayoutRequest request;
  request.set_pipeline(config.pipeline);
  request.set_sample_rate(config.sample_rate);
  request.set_num_channels(config.num_channels);
  request.set_frame_duration_ms(config.frame_duration_ms);
  return FfiClient::instance().newAudioPlayout(request);
}

std::shared_ptr<GstreamerAudioPlayoutSink> GstreamerAudioPlayoutSink::fromHandle(std::uint64_t handle) {
  std::shared_ptr<GstreamerAudioPlayoutSink> sink(new GstreamerAudioPlayoutSink());
  sink->impl_->handle = FfiHandle(static_cast<uintptr_t>(handle));
  return sink;
}

std::future<std::string> GstreamerAudioPlayoutSink::attachTrack(const RemoteAudioTrack& track) {
  return FfiClient::instance().attachAudioPlayoutTrackAsync(impl_->handle.get(), track.ffiHandleId());
}

std::future<bool> GstreamerAudioPlayoutSink::detachTrack(const std::string& track_id) {
  return FfiClient::instance().detachAudioPlayoutTrackAsync(impl_->handle.get(), track_id);
}

std::future<AudioPlayoutStats> GstreamerAudioPlayoutSink::stop() {
  return FfiClient::instance().stopAudioPlayoutAsync(impl_->handle.get());
}

} // namespace livekit::capture
