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

#include <vector>

#include "livekit/capture_source.h"
#include "livekit/visibility.h"

namespace livekit {

namespace proto {
class CaptureDeviceList;
}

/// Convert an FFI capture-device list to public device information.
///
/// Internal test seam; not part of the public SDK API.
LIVEKIT_INTERNAL_API std::vector<CaptureDeviceInfo> fromProto(const proto::CaptureDeviceList& list);

} // namespace livekit
