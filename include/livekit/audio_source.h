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
   *
   * Buffering behavior:
   * -------------------
   * - queue_size_ms == 0 (recommended for real-time capture):
   *     Disables internal buffering entirely. Audio frames are forwarded
   *     directly to WebRTC sinks and consumed synchronously.
   *
   *     This mode is optimized for real-time audio capture driven by hardware
   *     media callbacks (e.g. microphone capture). The caller is expected to
   *     provide fixed-size real-time frames (typically 10 ms per call).
   *
   *     Because the native side consumes frames immediately, this mode
   * minimizes latency and jitter and is the best choice for live capture
   * scenarios.
   *
   * - queue_size_ms > 0 (buffered / blocking mode):
   *     Enables an internal queue that buffers audio up to the specified
   * duration. Frames are accumulated and flushed asynchronously once the buffer
   * reaches its threshold.
   *
   *     This mode is intended for non-real-time producers (e.g. TTS engines,
   *     file-based audio, or agents generating audio faster or slower than
   *     real-time). The buffering layer smooths timing and allows the audio to
   * be streamed out in real time even if the producer is bursty.
   *
   *     queue_size_ms must be a multiple of 10.
   */
  AudioSource(int sample_rate, int num_channels, int queue_size_ms = 0);
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
   * Blocking semantics:
   * The blocking behavior of this call depends on the buffering mode selected
   * at construction time:
   *
   * - queue_size_ms == 0 (real-time capture mode):
   *     Frames are consumed synchronously by the native layer. The FFI callback
   *     is invoked immediately as part of the capture call, so this function
   *     returns quickly.
   *
   *     This mode relies on the caller being paced by a real-time media
   * callback (e.g. audio hardware interrupt / capture thread). It provides the
   * lowest possible latency and is ideal for live microphone capture.
   *
   * - queue_size_ms > 0 (buffered / non-real-time mode):
   *     Frames are queued internally and flushed asynchronously. This function
   *     will block until the buffered audio corresponding to this frame has
   * been consumed by the native side and the FFI callback fires.
   *
   *     This mode is best suited for non-real-time audio producers (such as TTS
   *     engines or agents) that generate audio independently of real-time
   * pacing, while still streaming audio out in real time.
   *
   * Safety notes:
   * May throw std::runtime_error if:
   *   - the FFI reports an error
   *   - a timeout occurs in bounded-wait mode
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
