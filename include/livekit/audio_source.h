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

#pragma once

#include <cstdint>

#include "livekit/audio_frame.h"
#include "livekit/ffi_handle.h"

namespace livekit {

namespace proto {
class FfiRequest;
class FfiResponse;
} // namespace proto

class FfiClient;

/**
 * Represents a real-time audio source with an internal audio queue.
 */
class AudioSource {
public:
  /**
   * Create a new native audio source.
   *
   * @param sample_rate   Sample rate in Hz.
   * @param num_channels  Number of channels.
   * @param queue_size_ms Max buffer duration for the internal queue in ms.
   */
  AudioSource(int sample_rate, int num_channels, int queue_size_ms = 1000);

  ~AudioSource();

  AudioSource(const AudioSource &) = delete;
  AudioSource &operator=(const AudioSource &) = delete;
  AudioSource(AudioSource &&) noexcept = default;
  AudioSource &operator=(AudioSource &&) noexcept = default;

  /// The sample rate of the audio source in Hz.
  int sample_rate() const noexcept { return sample_rate_; }

  /// The number of audio channels.
  int num_channels() const noexcept { return num_channels_; }

  /// Underlying FFI handle ID used in FFI requests.
  std::uint64_t ffi_handle_id() const noexcept {
    return static_cast<std::uint64_t>(handle_.get());
  }

  /// Current duration of queued audio (in seconds).
  double queuedDuration() const noexcept;

  /**
   * Clears the internal audio queue on the native side and resets local
   * queue tracking.
   */
  void clearQueue();

  /**
   * Push an AudioFrame into the audio source.
   *
   * It sends a capture_audio_frame FFI request and may throw on error
   * (depending on what the FFI response exposes).
   *
   * If the frame has zero samples, this method is a no-op.
   *
   * @throws std::runtime_error on FFI-reported error (if available).
   */
  void captureFrame(const AudioFrame &frame);

  /**
   * Block until the currently queued audio has (roughly) played out.
   */
  void waitForPlayout() const;

private:
  // Internal helper to reset the local queue tracking (like _release_waiter).
  void resetQueueTracking() noexcept;

  int sample_rate_;
  int num_channels_;
  int queue_size_ms_;

  // RAII wrapper for this audio source's FFI handle
  FfiHandle handle_;

  // Queue tracking (all in seconds; based on steady_clock in the .cpp).
  mutable double last_capture_{0.0};
  mutable double q_size_{0.0};
};

} // namespace livekit
