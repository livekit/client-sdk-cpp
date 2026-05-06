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

#ifndef LIVEKIT_SCOPED_TIMER_H
#define LIVEKIT_SCOPED_TIMER_H

#include "lk_log.h"

#include <chrono>
#include <string_view>

namespace livekit::detail {

// RAII timer that logs its label and elapsed microseconds at LK_LOG_DEBUG
// when it goes out of scope.
//
// Intended for ad hoc instrumentation of hot paths (e.g. the data-track
// FFI hop). To see output, raise the runtime log level to debug before
// running the test, e.g.:
//
//   livekit::setLogLevel(livekit::LogLevel::Debug);
//
// In release builds compiled with -DLIVEKIT_LOG_LEVEL=INFO (or higher),
// the underlying LK_LOG_DEBUG call is stripped at compile time and this
// class collapses to a steady_clock pair the optimizer is free to drop.
//
// `label` is captured as a string_view; only string literals or storage
// that outlives the scope are safe.
class ScopedTimer {
public:
  explicit ScopedTimer(std::string_view label) noexcept
      : label_(label), start_(std::chrono::steady_clock::now()) {}

  ~ScopedTimer() {
    const auto elapsed = std::chrono::steady_clock::now() - start_;
    const auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    LK_LOG_DEBUG("[scoped_timer] {} took {} us", label_, us);
  }

  ScopedTimer(const ScopedTimer &) = delete;
  ScopedTimer &operator=(const ScopedTimer &) = delete;
  ScopedTimer(ScopedTimer &&) = delete;
  ScopedTimer &operator=(ScopedTimer &&) = delete;

private:
  std::string_view label_;
  std::chrono::steady_clock::time_point start_;
};

} // namespace livekit::detail

#define LK_SCOPED_TIMER_CONCAT2(a, b) a##b
#define LK_SCOPED_TIMER_CONCAT(a, b) LK_SCOPED_TIMER_CONCAT2(a, b)

// Drop one of these at the top of any scope to time it. Multiple per
// function are fine; pick distinct labels.
#define LK_SCOPED_TIMER(label)                                                 \
  ::livekit::detail::ScopedTimer LK_SCOPED_TIMER_CONCAT(_lk_scoped_timer_,     \
                                                        __LINE__) {            \
    label                                                                      \
  }

#endif /* LIVEKIT_SCOPED_TIMER_H */
