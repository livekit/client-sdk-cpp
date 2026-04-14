# AGENTS.md — LiveKit C++ Client SDK

## Project Overview

This is **client-sdk-cpp**, the official LiveKit C++ client SDK. It wraps a Rust core (`client-sdk-rust/`) via a protobuf-based FFI bridge. All WebRTC, networking, and media logic lives in Rust; the C++ layer provides an ergonomic API for C++ consumers.

## Architecture

### Core Principle
Rust owns as much of the business logic as possible. If a feature may be used by another SDK it should be implemented in Rust. Since this is an SDK,
ensure backwards compatibility is maintained when possible.


### Platform Support
The SDK must be supported on the following platforms:
- windows x64
- linux x86_64
- linux arm64
- macOS x86_64
- macOS arm64


SDK features follow pattern:

1. Serialize a protobuf `FfiRequest` message.
2. Send it to Rust via `FfiClient::instance().sendRequest(req)`.
3. Receive a synchronous `FfiResponse` or an asynchronous `FfiEvent` callback.
4. Expose the result through the C++ API.

When making larger scale changes, check with the developer before committing to architecture changes involving changes to the `client-sdk-rust/` submodule.

### Directory Layout
Be sure to update the directory layout in this file if the directory layout changes.

| Path | Description |
|------|-------------|
| `include/livekit/` | Public API headers (what SDK consumers include) |
| `src/` | Implementation files and internal-only headers (`ffi_client.h`, `lk_log.h`, etc.) |
| `src/tests/` | Google Test integration and stress tests |
| `examples/` | In-tree example applications |
| `bridge/` | **Deprecated** C-style bridge layer — do not add new functionality |
| `client-sdk-rust/` | Git submodule holding the Rust core of the SDK|
| `client-sdk-rust/livekit-ffi/protocol/*.proto` | FFI contract (protobuf definitions, read-only reference) |
| `cmake/` | Build helpers (`protobuf.cmake`, `spdlog.cmake`, `LiveKitConfig.cmake.in`) |
| `docker/` | Dockerfile for CI and SDK distribution images |
| `.github/workflows/` | GitHub Actions CI workflows |

### Key Types

- **`FfiClient`** — Singleton that sends FFI requests to Rust and dispatches event callbacks. Defined in `src/ffi_client.h` (internal).
- **`FfiHandle`** — RAII wrapper for Rust-owned resource handles; drop releases the FFI resource.
- **`Room`** — Central object for connecting to a LiveKit server room.
- **`RoomDelegate`** — Virtual callback interface for room lifecycle events.
- **`SubscriptionThreadDispatcher`** — Owns callback registrations and per-subscription reader threads for audio, video, and data tracks. `Room` delegates callback management here.
- **`LocalParticipant` / `RemoteParticipant`** — Participant objects for publishing and receiving tracks.

## Build
for building, use the build.sh script for Linux and macOS, and the build.cmd script for Windows. Do not invoke CMake directly to build the SDK.
Updates to ./build.sh and ./build.cmd should be accompanied by updates to this file and the README.md file.

```
./build.sh debug              # Debug build
./build.sh debug-tests        # Debug build with tests
./build.sh debug-examples     # Debug build with examples
./build.sh release            # Release build
./build.sh release-tests      # Release build with tests
./build.sh release-examples   # Release build with examples
./build.sh build-all          # All of the above
./build.sh clean              # Clean build artifacts
./build.sh clean-all          # Full clean (C++ + Rust targets)
```

**Requirements:** CMake 3.20+, C++17, Rust toolchain (cargo), protoc. On macOS: `brew install cmake ninja protobuf abseil spdlog`. On Linux: see the CI workflow for apt packages.

### SDK Packaging

```
./build.sh release --bundle --prefix /path/to/install
```

This installs headers, libraries, and CMake config files so downstream projects can use `find_package(LiveKit CONFIG)`.

## Coding Conventions

### Copyright Header

All source files must have the LiveKit Apache 2.0 copyright header. Use the current year for new files. Do not copyright externally pulled/cloned files.

### Logging

- Use `LK_LOG_*` macros from `src/lk_log.h` (internal header, **not** public API).
- `LK_LOG_ERROR` — when something is broken.
- `LK_LOG_WARN` — when something is unexpected but recoverable.
- `LK_LOG_INFO` — initialization, critical state changes.
- `LK_LOG_DEBUG` — potentially useful diagnostic info.
- Do not spam logs. Keep it concise.
- In production/internal SDK code, do not use `std::cout`, `std::cerr`, `printf`, or raw spdlog calls; use the logging APIs instead. Tests may use standard streams sparingly for ad hoc diagnostics when appropriate.
- The public logging API is `include/livekit/logging.h` (`setLogLevel`, `setLogCallback`). spdlog is a **private** dependency — it must never leak into public headers.

### Public API Surface

- Keep public APIs small. Minimize what goes into `include/livekit/`.
- Never introduce backwards compatibility breaking changes to the public API.
- `lk_log.h` lives under `src/` (internal). The public-facing logging API is `include/livekit/logging.h`.
- spdlog must not appear in any public header or installed header.

### Error Handling

- Use `LK_LOG_WARN` for non-fatal unexpected conditions.
- Use `Result<T, E>` for operations that can fail with typed errors (e.g., data track publish/subscribe).

### Git Practices

- Use `git mv` when moving or renaming files.

### CMake

- spdlog is linked **PRIVATE** to the `livekit` target. It must not appear in exported/installed dependencies.
- protobuf is vendored via FetchContent on non-Windows platforms; Windows uses vcpkg.
- The CMake install produces a `find_package(LiveKit CONFIG)`-compatible package with `LiveKitConfig.cmake`, `LiveKitTargets.cmake`, and `LiveKitConfigVersion.cmake`.

### Readability and Performance
Code should be easy to read and understand. If a sacrifice is made for performance or readability, it should be documented.
## Dependencies

| Dependency | Scope | Notes |
|------------|-------|-------|
| protobuf | Private (built-in) | Vendored via FetchContent (Unix) or vcpkg (Windows) |
| spdlog | **Private** | FetchContent or system package; must NOT leak into public API |
| client-sdk-rust | Build-time | Git submodule, built via cargo during CMake build |
| Google Test | Test only | FetchContent in `src/tests/CMakeLists.txt` |

## Testing

Tests are under `src/tests/` using Google Test:

```
./build.sh debug-tests
cd build-debug && ctest
```

Integration tests (`src/tests/integration/`) cover: room connections, callbacks, data tracks, RPC, logging, audio processing, and the subscription thread dispatcher.

When adding new client facing functionality, add a new test case to the existing test suite.
When adding new client facing functionality, add benchmarking to understand the limitations of the new functionality.

## Formatting
Adhere to the formatting rules if TODO: @alan

## Deprecated / Out of Scope

- **`bridge/`** (`livekit_bridge`) is deprecated. Do not add new functionality to it.

## Common Pitfalls

- A `Room` with `auto_subscribe = false` will never receive remote audio/video frames — this is almost never what you want.
- A single participant cannot subscribe to its own tracks as "remote." Testing sender/receiver requires two separate room connections with distinct identities.
- macOS dylibs require `install_name_tool` fixups for `@rpath` — the CMakeLists.txt handles this automatically. Do not manually adjust RPATH unless you understand the implications.
- When consuming the installed SDK, use `-DLIVEKIT_LOCAL_SDK_DIR=/path/to/sdk` to point cmake at a local install instead of downloading a release tarball.
