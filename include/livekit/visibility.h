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

// LIVEKIT_API marks a symbol as part of the public ABI of liblivekit.
//
// On Unix, the SDK is built with -fvisibility=hidden / -fvisibility-inlines-hidden,
// so every symbol defaults to hidden. LIVEKIT_API re-exposes the symbol with
// default visibility. Consumers also need default visibility so public RTTI
// stays unique across shared-library boundaries.
//
// On Windows, the SDK is built without WINDOWS_EXPORT_ALL_SYMBOLS, so symbols
// must be explicitly tagged with __declspec(dllexport) when building the SDK
// and __declspec(dllimport) when consuming it. LIVEKIT_BUILDING_SDK is defined
// only when compiling the SDK itself (set in CMakeLists.txt).

#if defined(_WIN32)
#if defined(LIVEKIT_BUILDING_SDK)
#define LIVEKIT_API __declspec(dllexport)
#else
#define LIVEKIT_API __declspec(dllimport)
#endif
#else
#define LIVEKIT_API __attribute__((visibility("default")))
#endif

// LIVEKIT_INTERNAL_API marks a symbol that is NOT part of the public ABI but
// must remain visible so that the in-tree test binaries (which link against
// the same shared library) can resolve it.
//
// External consumers must not rely on LIVEKIT_INTERNAL_API symbols; they may
// change or disappear without notice.
//
// On Windows, internal symbols are exported the same way as public ones
// because tests link via the standard import library; on Unix, hidden
// visibility is overridden for these specific symbols only.

#if defined(_WIN32)
#define LIVEKIT_INTERNAL_API LIVEKIT_API
#else
#if defined(LIVEKIT_BUILDING_SDK)
#define LIVEKIT_INTERNAL_API __attribute__((visibility("default")))
#else
#define LIVEKIT_INTERNAL_API
#endif
#endif
