/*
 * Copyright 2026 LiveKit, Inc.
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

#include <optional>
#include <string>

///
/// Note: This adapter is a temporary internal translation unit for exercising UniFFI
/// bindings. It will be removed when the represented UniFFI APIs are migrated to actual
/// SDK features.
///

#include "livekit/visibility.h"

namespace livekit {

/// @brief Gets the build version through the generated UniFFI binding.
///
/// @return The generated binding's build version, or no value if it is invalid
/// or the binding call fails.
LIVEKIT_INTERNAL_API std::optional<std::string> uniffiBindgenBuildVersion();

} // namespace livekit
