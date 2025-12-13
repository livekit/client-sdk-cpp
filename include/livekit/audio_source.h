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
  virtual ~AudioSource() = default;

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
   * Push an AudioFrame into the audio source and BLOCK until the FFI callback
   * confirms that the native side has finished processing (consuming) the
   * frame. Safe usage: The frame's internal buffer must remain valid only until
   * this function returns. Because this call blocks until the corresponding FFI
   * callback arrives, the caller may safely destroy or reuse the frame
   * afterward.
   * @param frame       The audio frame to send. No-op if the frame contains
   * zero samples.
   * @param timeout_ms  Maximum time to wait for the FFI callback.
   *                    - If timeout_ms > 0: block up to this duration.
   *                      A timeout will cause std::runtime_error.
   *                    - If timeout_ms == 0: wait indefinitely until the
   * callback arrives (recommended for production unless the caller needs
   * explicit timeout control).
   *
   * Notes:
   *   - This is a blocking call.
   *   - timeout_ms == 0 (infinite wait) is the safest mode because it
   * guarantees the callback completes before the function returns, which in
   * turn guarantees that the audio buffer lifetime is fully protected. The
   * caller does not need to manage or extend the frame lifetime manually.
   *
   *   - May throw std::runtime_error if:
   *       • the FFI reports an error
   *
   *   - The underlying FFI request *must* eventually produce a callback for
   * each frame. If the FFI layer is misbehaving or the event loop is stalled,
   *     a timeout may occur in bounded-wait mode.
   */
  void captureFrame(const AudioFrame &frame, int timeout_ms = 20);

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
