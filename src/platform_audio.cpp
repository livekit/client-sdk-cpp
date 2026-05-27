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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "livekit/platform_audio.h"

#include <utility>

#include "ffi.pb.h"
#include "ffi_client.h"

namespace livekit {

struct PlatformAudioState {
  FfiHandle handle;
  std::int32_t recording_device_count = 0;
  std::int32_t playout_device_count = 0;
};

namespace {

std::uint64_t requireHandle(const std::shared_ptr<PlatformAudioState>& state) {
  if (!state || !state->handle) {
    throw PlatformAudioError("PlatformAudio has no valid FFI handle");
  }
  return static_cast<std::uint64_t>(state->handle.get());
}

AudioDeviceInfo fromProto(const proto::AudioDeviceInfo& device) {
  AudioDeviceInfo out;
  out.index = device.index();
  out.name = device.name();
  out.id = device.has_guid() ? device.guid() : std::string();
  return out;
}

std::vector<AudioDeviceInfo> convertDevices(const google::protobuf::RepeatedPtrField<proto::AudioDeviceInfo>& devices) {
  std::vector<AudioDeviceInfo> out;
  out.reserve(static_cast<std::size_t>(devices.size()));
  for (const auto& device : devices) {
    out.push_back(fromProto(device));
  }
  return out;
}

proto::AudioSourceOptions toProto(const PlatformAudioOptions& options) {
  proto::AudioSourceOptions out;
  out.set_echo_cancellation(options.echo_cancellation);
  out.set_noise_suppression(options.noise_suppression);
  out.set_auto_gain_control(options.auto_gain_control);
  out.set_prefer_hardware(options.prefer_hardware);
  return out;
}

} // namespace

PlatformAudioSource::PlatformAudioSource(FfiHandle handle, std::shared_ptr<PlatformAudioState> platform_audio) noexcept
    : handle_(std::move(handle)), platform_audio_(std::move(platform_audio)) {}

PlatformAudio::PlatformAudio() {
  proto::FfiRequest req;
  req.mutable_new_platform_audio();

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_new_platform_audio()) {
    throw PlatformAudioError("FfiResponse missing new_platform_audio");
  }

  const auto& platform_audio = resp.new_platform_audio();
  if (platform_audio.has_error()) {
    throw PlatformAudioError(platform_audio.error());
  }
  if (!platform_audio.has_platform_audio()) {
    throw PlatformAudioError("NewPlatformAudioResponse missing platform_audio");
  }

  const auto& owned = platform_audio.platform_audio();
  state_ = std::make_shared<PlatformAudioState>();
  state_->handle = FfiHandle(static_cast<uintptr_t>(owned.handle().id()));
  state_->recording_device_count = owned.info().recording_device_count();
  state_->playout_device_count = owned.info().playout_device_count();
}

std::int32_t PlatformAudio::recordingDeviceCount() const noexcept {
  return state_ ? state_->recording_device_count : 0;
}

std::int32_t PlatformAudio::playoutDeviceCount() const noexcept { return state_ ? state_->playout_device_count : 0; }

std::vector<AudioDeviceInfo> PlatformAudio::recordingDevices() const {
  proto::FfiRequest req;
  req.mutable_get_audio_devices()->set_platform_audio_handle(requireHandle(state_));

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_get_audio_devices()) {
    throw PlatformAudioError("FfiResponse missing get_audio_devices");
  }

  const auto& devices = resp.get_audio_devices();
  if (devices.has_error() && !devices.error().empty()) {
    throw PlatformAudioError(devices.error());
  }
  return convertDevices(devices.recording_devices());
}

std::vector<AudioDeviceInfo> PlatformAudio::playoutDevices() const {
  proto::FfiRequest req;
  req.mutable_get_audio_devices()->set_platform_audio_handle(requireHandle(state_));

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_get_audio_devices()) {
    throw PlatformAudioError("FfiResponse missing get_audio_devices");
  }

  const auto& devices = resp.get_audio_devices();
  if (devices.has_error() && !devices.error().empty()) {
    throw PlatformAudioError(devices.error());
  }
  return convertDevices(devices.playout_devices());
}

void PlatformAudio::setRecordingDevice(const std::string& device_id) const {
  proto::FfiRequest req;
  auto* msg = req.mutable_set_recording_device();
  msg->set_platform_audio_handle(requireHandle(state_));
  msg->set_device_id(device_id);

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_set_recording_device()) {
    throw PlatformAudioError("FfiResponse missing set_recording_device");
  }

  const auto& set_device = resp.set_recording_device();
  if (set_device.has_error() && !set_device.error().empty()) {
    throw PlatformAudioError(set_device.error());
  }
}

void PlatformAudio::setPlayoutDevice(const std::string& device_id) const {
  proto::FfiRequest req;
  auto* msg = req.mutable_set_playout_device();
  msg->set_platform_audio_handle(requireHandle(state_));
  msg->set_device_id(device_id);

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_set_playout_device()) {
    throw PlatformAudioError("FfiResponse missing set_playout_device");
  }

  const auto& set_device = resp.set_playout_device();
  if (set_device.has_error() && !set_device.error().empty()) {
    throw PlatformAudioError(set_device.error());
  }
}

std::shared_ptr<PlatformAudioSource> PlatformAudio::createAudioSource(const PlatformAudioOptions& options) const {
  proto::FfiRequest req;
  auto* msg = req.mutable_new_audio_source();
  msg->set_type(proto::AudioSourceType::AUDIO_SOURCE_PLATFORM);
  msg->set_platform_audio_handle(requireHandle(state_));
  *msg->mutable_options() = toProto(options);

  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_new_audio_source()) {
    throw PlatformAudioError("FfiResponse missing new_audio_source");
  }

  const auto& new_source = resp.new_audio_source();
  if (!new_source.has_source() || !new_source.source().has_handle()) {
    throw PlatformAudioError("NewAudioSourceResponse missing source handle");
  }

  const auto& source = new_source.source();
  const std::uint64_t handle_id = source.handle().id();
  if (handle_id == 0) {
    throw PlatformAudioError("NewAudioSourceResponse returned an invalid (null) source handle");
  }

  FfiHandle handle(static_cast<uintptr_t>(handle_id));
  return std::shared_ptr<PlatformAudioSource>(new PlatformAudioSource(std::move(handle), state_));
}

} // namespace livekit
