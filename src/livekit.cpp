/*
 * Copyright 2023 LiveKit
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

#include "livekit/livekit.h"
#include "ffi_client.h"

namespace livekit {

void initializeLogger();
void shutdownLogger();

bool initialize(LogLevel level) {
  initializeLogger();
  setLogLevel(level);
  auto &ffi_client = FfiClient::instance();
  return ffi_client.initialize(/*capture_logs=*/false);
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

bool initialize(LogSink log_sink) {
  initializeLogger();
  auto &ffi_client = FfiClient::instance();
  return ffi_client.initialize(log_sink == LogSink::kCallback);
}

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

void shutdown() {
  auto &ffi_client = FfiClient::instance();
  ffi_client.shutdown();
  shutdownLogger();
}

} // namespace livekit