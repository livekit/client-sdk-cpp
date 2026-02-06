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

#pragma once

#include <cstdint>

#include "livekit/audio_frame.h"
#include "livekit/ffi_handle.h"

namespace livekit {

/**
 * @brief WebRTC Audio Processing Module (APM) for real-time audio enhancement.
 *
 * AudioProcessingModule exposes WebRTC's built-in audio processing capabilities
 * including echo cancellation, noise suppression, automatic gain control, and
 * high-pass filtering.
 *
 * This class is designed for scenarios where you need explicit control over
 * audio processing, separate from the built-in processing in AudioSource.
 *
 * Typical usage pattern for echo cancellation:
 * 1. Create an APM with desired features enabled
 * 2. Call processReverseStream() with speaker/playback audio (reference signal)
 * 3. Call processStream() with microphone audio (near-end signal)
 * 4. The processed microphone audio will have echo removed
 *
 * Note: Audio frames must be exactly 10ms in duration.
 */
class AudioProcessingModule {
public:
  /**
   * @brief Configuration options for the Audio Processing Module.
   */
  struct Options {
    /// Enable acoustic echo cancellation (AEC3).
    /// Removes acoustic echo in two-way communication scenarios.
    bool echo_cancellation = false;

    /// Enable noise suppression.
    /// Reduces background noise from non-speech sources.
    bool noise_suppression = false;

    /// Enable high-pass filter.
    /// Removes low-frequency noise below ~80 Hz (DC offset, rumble).
    bool high_pass_filter = false;

    /// Enable automatic gain control (AGC).
    /// Auto-adjusts microphone gain to maintain consistent audio levels.
    bool auto_gain_control = false;

    /// Default constructor.
    Options() = default;
  };

  /**
   * @brief Create a new Audio Processing Module with default options (all
   * disabled).
   *
   * @throws std::runtime_error if the APM could not be created.
   */
  AudioProcessingModule();

  /**
   * @brief Create a new Audio Processing Module with the specified options.
   *
   * @param options Configuration for which processing features to enable.
   * @throws std::runtime_error if the APM could not be created.
   */
  explicit AudioProcessingModule(const Options &options);

  virtual ~AudioProcessingModule() = default;

  // Non-copyable
  AudioProcessingModule(const AudioProcessingModule &) = delete;
  AudioProcessingModule &operator=(const AudioProcessingModule &) = delete;

  // Movable
  AudioProcessingModule(AudioProcessingModule &&) noexcept = default;
  AudioProcessingModule &operator=(AudioProcessingModule &&) noexcept = default;

  /**
   * @brief Process the forward (near-end/microphone) audio stream.
   *
   * This method processes audio captured from the local microphone. It applies
   * the enabled processing features (noise suppression, gain control, etc.)
   * and removes echo based on the reference signal provided via
   * processReverseStream().
   *
   * The audio data is modified in-place.
   *
   * @param frame The audio frame to process (modified in-place).
   *
   * @throws std::runtime_error if processing fails.
   *
   * @note The frame must contain exactly 10ms of audio.
   */
  void processStream(AudioFrame &frame);

  /**
   * @brief Process the reverse (far-end/speaker) audio stream.
   *
   * This method provides the reference signal for echo cancellation. Call this
   * with the audio that is being played through the speakers, so the APM can
   * learn the acoustic characteristics and remove the echo from the microphone
   * signal.
   *
   * The audio data is modified in-place.
   *
   * @param frame The audio frame to process (modified in-place).
   *
   * @throws std::runtime_error if processing fails.
   *
   * @note The frame must contain exactly 10ms of audio.
   */
  void processReverseStream(AudioFrame &frame);

  /**
   * @brief Set the estimated delay between the reverse and forward streams.
   *
   * This must be called if and only if echo processing is enabled.
   *
   * Sets the delay in ms between processReverseStream() receiving a far-end
   * frame and processStream() receiving a near-end frame containing the
   * corresponding echo. On the client-side this can be expressed as:
   *
   *   delay = (t_render - t_analyze) + (t_process - t_capture)
   *
   * where:
   *   - t_analyze is the time a frame is passed to processReverseStream() and
   *     t_render is the time the first sample of the same frame is rendered by
   *     the audio hardware.
   *   - t_capture is the time the first sample of a frame is captured by the
   *     audio hardware and t_process is the time the same frame is passed to
   *     processStream().
   *
   * @param delay_ms Delay in milliseconds.
   *
   * @throws std::runtime_error if setting the delay fails.
   */
  void setStreamDelayMs(int delay_ms);

private:
  /// Check if the APM handle is valid (used internally).
  bool valid() const noexcept { return handle_.valid(); }

  /// Get the underlying FFI handle ID (used internally).
  std::uint64_t ffi_handle_id() const noexcept {
    return static_cast<std::uint64_t>(handle_.get());
  }

  FfiHandle handle_;
};

} // namespace livekit
