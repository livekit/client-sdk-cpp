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

#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "livekit/ffi_handle.h"
#include "livekit/visibility.h"

namespace livekit {

/// Internal shared state for platform audio handles.
///
/// This forward declaration is exposed only so public wrapper types can hold a
/// shared implementation pointer.
///
/// @note This object is thread-safe.
struct PlatformAudioState;

/// Information about a platform audio device.
///
/// @note Device indices may change when audio hardware is added or removed. Use
/// the stable `id` value when selecting a device.
struct AudioDeviceInfo {
  /// Current device index.
  std::uint32_t index = 0;

  /// Device name reported by the operating system.
  std::string name;

  /// Platform-specific stable device identifier.
  std::string id;
};

/// Audio processing options for platform microphone capture.
///
/// The default values enable WebRTC's voice processing path for typical
/// microphone publishing.
struct PlatformAudioOptions {
  /// Enable acoustic echo cancellation.
  bool echo_cancellation = true;

  /// Enable background noise suppression.
  bool noise_suppression = true;

  /// Enable automatic gain control.
  bool auto_gain_control = true;

  /// Prefer hardware audio processing when the platform provides it.
  bool prefer_hardware = false;
};

/// Error raised when platform audio setup or device operations fail.
class LIVEKIT_API PlatformAudioError : public std::runtime_error {
public:
  /// Create a platform audio error.
  ///
  /// @param message  Human-readable error message.
  explicit PlatformAudioError(const std::string& message) : std::runtime_error(message) {}
};

/// Audio source backed by WebRTC's platform Audio Device Module.
///
/// A PlatformAudioSource captures microphone audio automatically. Unlike
/// AudioSource, callers do not push frames with captureFrame().
class LIVEKIT_API PlatformAudioSource {
public:
  /// Copy construction is disabled.
  PlatformAudioSource(const PlatformAudioSource& other) = delete;

  /// Copy assignment is disabled.
  PlatformAudioSource& operator=(const PlatformAudioSource& other) = delete;

  /// Move the platform audio source.
  ///
  /// @param other  Source to move from.
  ///
  /// @note This operation is not thread-safe.
  PlatformAudioSource(PlatformAudioSource&& other) noexcept = default;

  /// Move-assign the platform audio source.
  ///
  /// @param other  Source to move from.
  /// @return Reference to this source.
  ///
  /// @note This operation is not thread-safe.
  PlatformAudioSource& operator=(PlatformAudioSource&& other) noexcept = default;

  /// Return the underlying FFI handle ID used in FFI requests.
  ///
  /// @note This operation is thread-safe.
  std::uint64_t ffiHandleId() const noexcept { return static_cast<std::uint64_t>(handle_.get()); }

private:
  friend class PlatformAudio;

  PlatformAudioSource(FfiHandle handle, std::shared_ptr<PlatformAudioState> platform_audio) noexcept;

  FfiHandle handle_;
  std::shared_ptr<PlatformAudioState> platform_audio_;
};

/// Platform audio device manager backed by WebRTC's Audio Device Module.
///
/// Use PlatformAudio for microphone capture when built-in echo
/// cancellation, noise suppression, automatic gain control, and speaker playout
/// are desired. Use AudioSource instead when the application needs direct access
/// to raw PCM frames or custom audio generation.
class LIVEKIT_API PlatformAudio {
public:
  /// Create a platform audio manager.
  ///
  /// Enables WebRTC's platform Audio Device Module for microphone capture and
  /// speaker playout.
  ///
  /// @throws PlatformAudioError  If the FFI response is malformed or the
  /// platform Audio Device Module cannot be created.
  ///
  /// @note This operation is thread-safe.
  PlatformAudio();

  /// Copy the platform audio manager.
  ///
  /// The copy shares the same underlying platform audio handle.
  ///
  /// @param other  Manager to copy from.
  ///
  /// @note This operation is not thread-safe.
  PlatformAudio(const PlatformAudio& other) = default;

  /// Copy-assign the platform audio manager.
  ///
  /// The assigned instance shares the same underlying platform audio handle.
  ///
  /// @param other  Manager to copy from.
  /// @return Reference to this manager.
  ///
  /// @note This operation is not thread-safe.
  PlatformAudio& operator=(const PlatformAudio& other) = default;

  /// Move the platform audio manager.
  ///
  /// @param other  Manager to move from.
  ///
  /// @note This operation is not thread-safe.
  PlatformAudio(PlatformAudio&& other) noexcept = default;

  /// Move-assign the platform audio manager.
  ///
  /// @param other  Manager to move from.
  /// @return Reference to this manager.
  ///
  /// @note This operation is not thread-safe.
  PlatformAudio& operator=(PlatformAudio&& other) noexcept = default;

  /// Return the current number of recording devices.
  ///
  /// @throws PlatformAudioError  If the FFI response is malformed or device
  /// enumeration fails.
  ///
  /// @note This operation is thread-safe.
  std::int32_t recordingDeviceCount() const;

  /// Return the current number of playout devices.
  ///
  /// @throws PlatformAudioError  If the FFI response is malformed or device
  /// enumeration fails.
  ///
  /// @note This operation is thread-safe.
  std::int32_t playoutDeviceCount() const;

  /// Enumerate available microphones.
  ///
  /// @return List of available recording devices.
  /// @throws PlatformAudioError  If the FFI response is malformed or device
  /// enumeration fails.
  ///
  /// @note This operation is thread-safe.
  std::vector<AudioDeviceInfo> recordingDevices() const;

  /// Enumerate available speakers/headphones.
  ///
  /// @return List of available playout devices.
  /// @throws PlatformAudioError  If the FFI response is malformed or device
  /// enumeration fails.
  ///
  /// @note This operation is thread-safe.
  std::vector<AudioDeviceInfo> playoutDevices() const;

  /// Select the microphone by device ID.
  ///
  /// @param device_id  Stable device identifier from AudioDeviceInfo::id.
  /// @throws PlatformAudioError  If the FFI response is malformed or device
  /// selection fails.
  ///
  /// @note This operation is thread-safe.
  void setRecordingDevice(const std::string& device_id) const;

  /// Select the speaker/headphones by device ID.
  ///
  /// @param device_id  Stable device identifier from AudioDeviceInfo::id.
  /// @throws PlatformAudioError  If the FFI response is malformed or device
  /// selection fails.
  ///
  /// @note This operation is thread-safe.
  void setPlayoutDevice(const std::string& device_id) const;

  /// Create an automatically captured microphone source for LocalAudioTrack.
  ///
  /// Each call returns a new track source handle backed by the shared platform
  /// Audio Device Module; it does not create a separate ADM instance.
  ///
  /// @param options  Audio processing options for the platform microphone path.
  /// @return Platform-backed audio source suitable for LocalAudioTrack.
  /// @throws PlatformAudioError  If the FFI response is malformed or source
  /// creation fails.
  ///
  /// @note This operation is thread-safe.
  std::shared_ptr<PlatformAudioSource> createAudioSource(const PlatformAudioOptions& options = {}) const;

private:
  std::shared_ptr<PlatformAudioState> state_;
};

} // namespace livekit
