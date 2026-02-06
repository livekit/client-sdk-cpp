/*
 * Copyright 2025 LiveKit
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

#include "livekit/audio_processing_module.h"

#include <stdexcept>

#include "audio_frame.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"

namespace livekit {

AudioProcessingModule::AudioProcessingModule()
    : AudioProcessingModule(Options{}) {}

AudioProcessingModule::AudioProcessingModule(const Options &options) {
  proto::FfiRequest req;
  auto *msg = req.mutable_new_apm();
  msg->set_echo_canceller_enabled(options.echo_cancellation);
  msg->set_noise_suppression_enabled(options.noise_suppression);
  msg->set_high_pass_filter_enabled(options.high_pass_filter);
  msg->set_gain_controller_enabled(options.auto_gain_control);

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);

  if (!resp.has_new_apm()) {
    throw std::runtime_error(
        "AudioProcessingModule: failed to create APM - no response");
  }

  const auto &apm_info = resp.new_apm().apm();
  handle_ = FfiHandle(static_cast<uintptr_t>(apm_info.handle().id()));

  if (!handle_.valid()) {
    throw std::runtime_error(
        "AudioProcessingModule: failed to create APM - invalid handle");
  }
}

void AudioProcessingModule::processStream(AudioFrame &frame) {
  if (!handle_.valid()) {
    throw std::runtime_error("AudioProcessingModule: invalid handle");
  }

  if (frame.data().empty()) {
    return;
  }

  proto::FfiRequest req;
  auto *msg = req.mutable_apm_process_stream();
  msg->set_apm_handle(static_cast<std::uint64_t>(handle_.get()));
  msg->set_data_ptr(reinterpret_cast<std::uint64_t>(frame.data().data()));
  msg->set_size(
      static_cast<std::uint32_t>(frame.data().size() * sizeof(std::int16_t)));
  msg->set_sample_rate(static_cast<std::uint32_t>(frame.sample_rate()));
  msg->set_num_channels(static_cast<std::uint32_t>(frame.num_channels()));

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);

  if (!resp.has_apm_process_stream()) {
    throw std::runtime_error(
        "AudioProcessingModule::processStream: unexpected response");
  }

  const auto &result = resp.apm_process_stream();
  if (result.has_error()) {
    throw std::runtime_error("AudioProcessingModule::processStream: " +
                             result.error());
  }
}

void AudioProcessingModule::processReverseStream(AudioFrame &frame) {
  if (!handle_.valid()) {
    throw std::runtime_error("AudioProcessingModule: invalid handle");
  }

  if (frame.data().empty()) {
    return;
  }

  proto::FfiRequest req;
  auto *msg = req.mutable_apm_process_reverse_stream();
  msg->set_apm_handle(static_cast<std::uint64_t>(handle_.get()));
  msg->set_data_ptr(reinterpret_cast<std::uint64_t>(frame.data().data()));
  msg->set_size(
      static_cast<std::uint32_t>(frame.data().size() * sizeof(std::int16_t)));
  msg->set_sample_rate(static_cast<std::uint32_t>(frame.sample_rate()));
  msg->set_num_channels(static_cast<std::uint32_t>(frame.num_channels()));

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);

  if (!resp.has_apm_process_reverse_stream()) {
    throw std::runtime_error(
        "AudioProcessingModule::processReverseStream: unexpected response");
  }

  const auto &result = resp.apm_process_reverse_stream();
  if (result.has_error()) {
    throw std::runtime_error("AudioProcessingModule::processReverseStream: " +
                             result.error());
  }
}

void AudioProcessingModule::setStreamDelayMs(int delay_ms) {
  if (!handle_.valid()) {
    throw std::runtime_error("AudioProcessingModule: invalid handle");
  }

  proto::FfiRequest req;
  auto *msg = req.mutable_apm_set_stream_delay();
  msg->set_apm_handle(static_cast<std::uint64_t>(handle_.get()));
  msg->set_delay_ms(delay_ms);

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);

  if (!resp.has_apm_set_stream_delay()) {
    throw std::runtime_error(
        "AudioProcessingModule::setStreamDelayMs: unexpected response");
  }

  const auto &result = resp.apm_set_stream_delay();
  if (result.has_error()) {
    throw std::runtime_error("AudioProcessingModule::setStreamDelayMs: " +
                             result.error());
  }
}

} // namespace livekit
