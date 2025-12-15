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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "livekit/audio_source.h"

#include <chrono>
#include <stdexcept>
#include <thread>

#include "audio_frame.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/audio_frame.h"

namespace livekit {

using Clock = std::chrono::steady_clock;

// Helper to get monotonic time in seconds (similar to time.monotonic()).
static double now_seconds() {
  auto now = Clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::duration<double>>(now).count();
}

// ============================================================================
// AudioSource
// ============================================================================

AudioSource::AudioSource(int sample_rate, int num_channels, int queue_size_ms)
    : sample_rate_(sample_rate), num_channels_(num_channels),
      queue_size_ms_(queue_size_ms) {
  proto::FfiRequest req;
  auto *msg = req.mutable_new_audio_source();
  msg->set_type(proto::AudioSourceType::AUDIO_SOURCE_NATIVE);
  msg->set_sample_rate(static_cast<std::uint32_t>(sample_rate_));
  msg->set_num_channels(static_cast<std::uint32_t>(num_channels_));
  msg->set_queue_size_ms(static_cast<std::uint32_t>(queue_size_ms_));

  proto::FfiResponse resp = FfiClient::instance().sendRequest(req);

  const auto &source_info = resp.new_audio_source().source();
  // Wrap FFI handle in RAII FfiHandle
  handle_ = FfiHandle(static_cast<uintptr_t>(source_info.handle().id()));
}

double AudioSource::queuedDuration() const noexcept {
  if (last_capture_ == 0.0) {
    return 0.0;
  }

  double now = now_seconds();
  double elapsed = now - last_capture_;
  double remaining = q_size_ - elapsed;
  return remaining > 0.0 ? remaining : 0.0;
}

void AudioSource::resetQueueTracking() noexcept {
  last_capture_ = 0.0;
  q_size_ = 0.0;
}

void AudioSource::clearQueue() {
  if (!handle_) {
    resetQueueTracking();
    return;
  }

  proto::FfiRequest req;
  auto *msg = req.mutable_clear_audio_buffer();
  msg->set_source_handle(static_cast<std::uint64_t>(handle_.get()));

  (void)FfiClient::instance().sendRequest(req);

  // Reset local queue tracking.
  resetQueueTracking();
}

void AudioSource::captureFrame(const AudioFrame &frame, int timeout_ms) {
  using namespace std::chrono_literals;
  if (!handle_) {
    return;
  }

  if (frame.samples_per_channel() == 0) {
    return;
  }

  // Queue tracking, same logic as before
  double now = now_seconds();
  double elapsed = (last_capture_ == 0.0) ? 0.0 : (now - last_capture_);
  double frame_duration = static_cast<double>(frame.samples_per_channel()) /
                          static_cast<double>(sample_rate_);
  q_size_ += frame_duration - elapsed;
  if (q_size_ < 0.0) {
    q_size_ = 0.0; // clamp
  }
  last_capture_ = now;

  // Build AudioFrameBufferInfo from the wrapper
  proto::AudioFrameBufferInfo buf = frame.toProto();
  // Use async FFI API and block until the callback completes
  auto fut = FfiClient::instance().captureAudioFrameAsync(handle_.get(), buf);
  if (timeout_ms == 0) {
    fut.get(); // may throw std::runtime_error from async layer
    return;
  }
  // This will throw std::runtime_error if the callback reported an error
  auto status = fut.wait_for(std::chrono::milliseconds(timeout_ms));
  if (status == std::future_status::ready ||
      status == std::future_status::deferred) {
    fut.get();
  } else { // std::future_status::timeout
    std::cerr << "captureAudioFrameAsync timed out after " << timeout_ms
              << " ms\n";
  }
}

} // namespace livekit
