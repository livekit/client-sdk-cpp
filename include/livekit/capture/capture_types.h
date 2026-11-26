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

#pragma once

#include <cstdint>
#include <string>

/// @brief Capture sources and their configuration types.
namespace livekit::capture {

/// @brief Why a capture ended without error.
enum class CaptureExit {
  /// @brief Stopped by the application or source destruction.
  Stopped = 0,
  /// @brief The producer reached the end of its stream.
  EndOfStream = 1,
};

/// @brief Terminal pump failure category; matches the FFI CaptureErrorKind enum.
enum class CaptureErrorKind {
  /// @brief The capture source failed or violated its contract.
  Source = 0,
  /// @brief The RTC audio or video source rejected a frame.
  Rtc = 1,
  /// @brief The pump thread panicked.
  Panicked = 2,
};

/// @brief Structured terminal pump failure reported by the Rust FFI.
struct CaptureError {
  /// @brief Human-readable error message.
  std::string message;

  /// @brief Error category.
  CaptureErrorKind kind = CaptureErrorKind::Source;
};

/// @brief Successful terminal statistics of a started capture.
struct CaptureResult {
  /// @brief Number of frames accepted by the RTC source.
  ///
  /// Audio frames contain 10 ms of PCM; video frames are pixel frames or
  /// encoded access units.
  std::uint64_t frames_captured = 0;

  /// @brief Why the capture ended successfully.
  CaptureExit exit = CaptureExit::Stopped;
};

} // namespace livekit::capture
