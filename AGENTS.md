# AGENTS.md — LiveKit C++ Client SDK

## Project Overview

This is **client-sdk-cpp**, the official LiveKit C++ client SDK. It wraps a Rust core (`client-sdk-rust/`) via a protobuf-based FFI bridge. All WebRTC, networking, and media logic lives in Rust; the C++ layer provides an ergonomic API for C++ consumers.

## Architecture

### Core Principle
Rust owns as much of the business logic as possible. The C++ layer should be a thin wrapper around the Rust core. If a feature may be used by another SDK it should be implemented in Rust. Since this is an SDK,
ensure backwards compatibility is maintained when possible. Do not update auto-generated code.

### Platform Support
The SDK must be supported on the following platforms:
- windows x64
- linux x86_64
- linux arm64
- macOS x86_64
- macOS arm64

### FFI Patterns
SDK features follow one of two FFI patterns:

**Synchronous calls:**
1. Serialize a protobuf `FfiRequest` message.
2. Send it to Rust via `FfiClient::instance().sendRequest(req)`.
3. Receive a `FfiResponse` with the result.
4. Expose the result through the C++ API.

**Asynchronous calls:**
1. Set up an async handler that listens for an event keyed by a `request_async_id`.
2. Serialize a protobuf `FfiRequest` message containing the `request_async_id`.
3. Send it to Rust via `FfiClient::instance().sendRequest(req)`.
4. Receive a synchronous `FfiResponse` (acknowledgement) and, later, an asynchronous `FfiEvent` callback with the actual result.
5. Expose the result through the C++ API.

When making larger scale changes, check with the developer before committing to architecture changes involving changes to the `client-sdk-rust/` submodule.

### Threading Model

The SDK has three categories of threads:

**FFI callback thread** — The Rust FFI layer calls `ffiEventCallback` from a Rust-managed thread (typically a Tokio runtime thread). This single entry point deserializes the `FfiEvent` and calls `FfiClient::pushEvent`, which:
1. Completes any pending async `std::promise` matched by `async_id`.
2. Invokes all registered `FfiClient` listeners (including `Room::onEvent`).

All `RoomDelegate` callbacks and stream handler callbacks (e.g., `registerTextStreamHandler`) are invoked on this FFI callback thread. **Handlers must not block**; spawn a background thread if synchronous work is needed.

**Per-subscription reader threads** — `SubscriptionThreadDispatcher` creates a dedicated `std::thread` for each active audio, video, or data track subscription. These threads block on `AudioStream::read()`, `VideoStream::read()`, or `DataTrackStream::read()` and invoke the registered `AudioFrameCallback`, `VideoFrameCallback`, or `DataFrameCallback` on that reader thread — not on the FFI callback thread. A hard limit of 20 concurrent reader threads is enforced.

**Application threads** — The calling thread for public API methods such as `Room::connect`, `LocalParticipant::publishTrack`, `AudioSource::captureFrame`, etc. These may block while waiting for FFI responses or future completion.

#### Thread-safety guarantees

| Component | Thread-safe? | Notes |
|-----------|-------------|-------|
| `FfiClient::sendRequest` | Yes (C++ side) | No C++ mutex; relies on the Rust FFI being safe for concurrent calls. Multiple threads may call concurrently. |
| `FfiClient` listener/async registration | Yes | Protected by internal `std::mutex`. |
| `Room` | Yes | Internal `std::mutex` protects all mutable state. `RoomDelegate` is called outside the lock. |
| `SubscriptionThreadDispatcher` | Yes | Internal `std::mutex` protects registrations and active readers. Thread joins happen outside the lock. |
| `AudioStream` / `VideoStream` / `DataTrackStream` | Yes | Internal `std::mutex` + `condition_variable` coordinate the FFI producer thread and the consumer reader thread. |
| `AudioSource::captureFrame` | No | Not safe to call concurrently from multiple threads. |
| `PlatformAudio` / `PlatformAudioSource` | Yes | Thin `sendRequest` wrappers over immutable FFI handle state; destruction and move operations must be externally synchronized. |
| `VideoSource::captureFrame` | No | Not safe to call concurrently from multiple threads. |
| `LocalAudioTrack` / `LocalVideoTrack` | No | Thin `sendRequest` wrappers with no internal synchronization. |
| `LocalDataTrack::tryPush` | No | Thin `sendRequest` wrapper with no internal synchronization. |
| `TextStreamWriter` / `ByteStreamWriter` | Serialized | `write()` is serialized by an internal `write_mutex_`. |

### Directory Layout
Be sure to update the directory layout in this file if the directory layout changes.

| Path | Description |
|------|-------------|
| `include/livekit/` | Public API headers (what SDK consumers include) |
| `src/` | Implementation files and internal-only headers (`ffi_client.h`, `lk_log.h`, etc.) |
| `src/tests/` | Google Test integration and stress tests |
| `examples/` | In-tree example applications |
| `client-sdk-rust/` | Git submodule holding the Rust core of the SDK|
| `cpp-tools/` | Git submodule holding shared LiveKit C++ clang-format / clang-tidy configs, scripts, and docs |
| `client-sdk-rust/livekit-ffi/protocol/*.proto` | FFI contract (protobuf definitions, read-only reference) |
| `cmake/` | Build helpers (`protobuf.cmake`, `spdlog.cmake`, `LiveKitConfig.cmake.in`) |
| `docker/` | Dockerfile for CI and SDK distribution images |
| `scripts/` | Local helper scripts and transition wrappers that delegate shared clang tooling to `cpp-tools/` |
| `docs/` | Documentation root. `docs/` holds hand-written long-form Markdown intended to also read well on GitHub. |
| `docs/doxygen/` | Doxygen tool config, theme assets, and Doxygen-only content (`Doxyfile`, `index.md` mainpage, `customization/*.css`, `customization/header.html`, `customization/favicon.ico`). Files here use Doxygen-only syntax (`@ref`, `@brief`, …) and are not intended for human reading on their own. |
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
./build.sh clean              # Clean build artifacts + local-install
./build.sh clean-all          # Full clean (C++ + local-install + Rust targets)
```

The build scripts pass an explicit job count to `cmake --build --parallel`. Set
`CMAKE_BUILD_PARALLEL_LEVEL` to override the default detected logical CPU count.

**Requirements:** CMake 3.20+, C++17, Rust toolchain (cargo), protoc. On macOS: `brew install cmake ninja protobuf abseil`. On Linux: see the CI workflow for apt packages. spdlog is vendored automatically via FetchContent (or vcpkg on Windows) to avoid system conflicts.

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
- Use `#pragma once` to guard headers.
- All publicly visible symbols must use `LIVEKIT_API` macro (via `include/livekit/visibility.h`).
- Never introduce backwards compatibility breaking changes to the public API.
- `lk_log.h` lives under `src/` (internal). The public-facing logging API is `include/livekit/logging.h`.
- spdlog must not appear in any public header or installed header.

#### Deprecating a public API

When superseding a public API (renaming, replacing, or removing in a future major
version), every retained back-compat shim must carry **both** annotations:

1. The C++11 `[[deprecated("...")]]` attribute so the compiler warns at every
   call site. The message should name the replacement (e.g.
   `"AudioFrame::sample_rate is deprecated; use AudioFrame::sampleRate instead"`).
2. A Doxygen `/// @deprecated Use <newName>() instead.` line immediately above
   the attribute so the generated docs render a deprecation callout and add the
   symbol to Doxygen's auto-generated *Deprecated List* page. Doxygen does not
   read the C++ attribute, so this line is required even though it duplicates
   information.

Example:

```cpp
/// @deprecated Use sampleRate() instead.
[[deprecated("AudioFrame::sample_rate is deprecated; use AudioFrame::sampleRate instead")]]
int sample_rate() const noexcept { return sampleRate(); }
```

Keep the prose consistent: `Use <newName>() instead.` Per-symbol deprecations
must use `///` (not `//`); only section-level asides (e.g. "Deprecated public
mutators" group headers) may stay as plain `//` comments.

### Include Conventions

- **Public headers (`include/livekit/*.h`) must include other public headers
  with the `livekit/` prefix**: `#include "livekit/track.h"`, never the bare
  `#include "track.h"` form. The prefixed spelling matches what consumers see
  (`#include <livekit/track.h>`), resolves through the standard `-I include/`
  search path rather than the implementation-defined "current directory first"
  rule of `#include "..."`, and avoids accidental name collisions with
  third-party headers that happen to share short names like `track.h`.
- Use double quotes for project headers (`#include "livekit/foo.h"`) and angle
  brackets for system / third-party headers (`#include <vector>`,
  `#include <gtest/gtest.h>`).
- Implementation files in `src/` may include internal headers without a
  prefix (`#include "ffi_client.h"`); they are not part of the public
  surface and live alongside their consumers.
- Test code (in-tree or external-style canaries) must consume public
  headers via `<livekit/foo.h>` to mirror real consumer usage

### Symbol Visibility / Exported ABI

The `livekit` shared library is built with hidden symbol visibility on all
platforms. Only symbols explicitly tagged with `LIVEKIT_API` (declared in
`include/livekit/visibility.h`) are part of the public ABI. Vendored static
dependencies are also compiled with hidden visibility so their symbols cannot 
leak into `liblivekit.{so,dylib,dll}`.

Rules for new code:

- Mark any new public class, free function, or out-of-line static method with
  `LIVEKIT_API`.  POD/aggregate structs and pure-inline classes do not need
  annotation because they emit no out-of-line symbols.
- For internal symbols that must remain visible to in-tree tests (the tests
  link the same shared library), use `LIVEKIT_INTERNAL_API`.  External
  consumers must not rely on these.
- Do not add `__declspec(dllexport)` or `__attribute__((visibility("default")))`
  by hand; always go through `LIVEKIT_API` / `LIVEKIT_INTERNAL_API`.
- On Windows, `WINDOWS_EXPORT_ALL_SYMBOLS` is **deliberately disabled** for the
  `livekit` target.  Use `LIVEKIT_API` to export.

The exported ABI is enforced by `.github/scripts/check_no_private_symbols.py`,
run from the `make-release.yml` "Symbol leak check" CI step so a leak blocks
the release build itself (it does not run on regular pushes/PRs). The script
fails if `nm`/`dumpbin` reports any exported symbol matching a forbidden
substring (currently `spdlog::`, `fmt::v`, `google::protobuf`, `absl::`). To
run it locally, point it at the built shared library:

```bash
python3 .github/scripts/check_no_private_symbols.py \
    build-release/lib/liblivekit.dylib   # or .so / livekit.dll
```

**When adding a new third-party library or vendored dependency to the SDK,
update `.github/scripts/check_no_private_symbols.py` to add a forbidden
substring pattern for the new dependency's namespace/symbol prefix.** The
denylist is intentionally explicit — a new dep that leaks symbols will
otherwise silently pollute `liblivekit.{so,dylib,dll}` and clash at runtime
with the same library loaded elsewhere in the host process.

### Error Handling

- Use `LK_LOG_WARN` for non-fatal unexpected conditions.
- Use `Result<T, E>` for operations that can fail with typed errors (e.g., data track publish/subscribe).

### Public API Documentation (Doxygen)

The public API (`include/livekit/*.h`) is what consumers read first and is also
published as a Doxygen site (`docs/doxygen/Doxyfile`, `.github/workflows/publish-docs.yml`).
Every doc comment in `include/livekit/` must use the rules below, and PRs that
add or modify public symbols are gated on these rules during review.

#### Comment style

- Use triple-slash `///` Doxygen comments. Do **not** use `/** ... */` Javadoc
  blocks or `/* ... */` block comments for documentation.
- Apache license headers stay as `/* ... */` block comments — they are not
  documentation.
- Section organization comments (e.g. `// ---- Accessors ----`,
  `// Read-only properties`) may stay as `//` since they do not document a
  specific symbol.
- Implementation comments inside `.cpp` files (non-Doxygen) may use `//`
  freely.
- Use Doxygen `@`-prefixed commands (`@param`, `@return`, `@throws`, `@note`,
  `@brief`, `@deprecated`, `@ref`, `@p`, `@c`). Do not use the equivalent
  `\`-prefixed forms in new code.

#### Required tags

- Document class/structs succinctly using `@brief Description.`
- Document functions/methods/namespaces succinctly using `@brief Description.`
- Document parameters using `@param name Description.`
- Document non-void return values using `@return Description.`
- Document thrown exceptions using `@throws ExceptionType When/why it's thrown.`
  Operations that can fail without throwing should return `Result<T, E>`
  (see Error Handling above) and the variants should be documented in the doc block.
- Free-text "Parameters:", "Returns:", "Throws:" sections in legacy comments
  must be converted to the corresponding `@param` / `@return` / `@throws`
  tags when the comment is touched.

#### Example

```cpp
/// Publish a local track to the room.
///
/// Blocks until the FFI publish response arrives.
///
/// @param track   Track to publish. Must be non-null.
/// @param options Publish options (codec, simulcast, etc.).
/// @throws std::runtime_error if the FFI reports an error.
void publishTrack(const std::shared_ptr<Track>& track, const TrackPublishOptions& options);
```

#### Deprecation comments

When superseding a public API, every retained back-compat shim must carry a
Doxygen `/// @deprecated Use <newName>() instead.` line so the generated docs
render a deprecation callout (see "Deprecating a public API" above).

#### Verifying locally

Run the docs build script from the repository root:

```bash
./scripts/generate-docs.sh
```

The output lands in `docs/doxygen/html/index.html`. The Doxyfile sets
`WARN_IF_UNDOCUMENTED = NO` (the 400+ "X is not documented" warnings for
internal symbols are too noisy to enforce) and `WARN_AS_ERROR = FAIL_ON_WARNINGS`,
so any other warning (broken `@ref`, unknown `@command`, unsupported HTML tag,
malformed table, missing `@param` on a documented function, …) fails the build.

### Integer Types

- Prefer fixed-width integer types from `<cstdint>` (`std::int32_t`, `std::uint64_t`, etc.) over raw primitive integer types when size or signedness matters.
- This applies in public APIs, FFI/protobuf-facing code, serialized payloads, handles, timestamps, IDs, and any cross-platform boundary where integer width must be explicit.
- Use raw primitive integer types only when the value is intentionally platform-sized or when preserving an existing public API is necessary for backwards compatibility.
- Do not change an existing public API from a raw primitive integer type to a fixed-width type for style consistency alone unless the compatibility impact has been reviewed.

### Git Practices

- Use `git mv` when moving or renaming files.

### CMake

- spdlog is linked **PRIVATE** to the `livekit` target. It must not appear in exported/installed dependencies.
- protobuf is vendored via FetchContent on non-Windows platforms; Windows uses vcpkg.
- The CMake install produces a `find_package(LiveKit CONFIG)`-compatible package with `LiveKitConfig.cmake`, `LiveKitTargets.cmake`, and `LiveKitConfigVersion.cmake`.

### Readability and Performance

Code should be easy to read and understand. If a sacrifice is made for performance or readability, it should be documented.

Adhere to clang-format checks configured in `.clang-format`, which is a symlink to `cpp-tools/configs/.clang-format`. Run `./cpp-tools/scripts/clang-format.sh --path src --path include --path benchmarks` if needed to confirm code styling. During the transition, `./scripts/clang-format.sh` and `./scripts/clang-format.sh --fix` remain compatibility wrappers.

### Static Analysis

Adhere to clang-tidy checks configured in `.clang-tidy`, which is a symlink to `cpp-tools/configs/.clang-tidy`. Run `./scripts/clang-tidy.sh` if needed to confirm code quality; the wrapper supplies this repo's build directory, file regex, and header filters to the shared `cpp-tools` script.

## Dependencies

| Dependency | Scope | Notes |
|------------|-------|-------|
| protobuf | Private (built-in) | Vendored via FetchContent (Unix) or vcpkg (Windows) |
| spdlog | **Private** | FetchContent or system package; must NOT leak into public API |
| client-sdk-rust | Build-time | Git submodule, built via cargo during CMake build |
| cpp-tools | Developer / CI | Git submodule containing shared LiveKit C++ formatting and static-analysis tooling |
| Google Test | Test only | FetchContent in `src/tests/CMakeLists.txt` |

When adding a new private/vendored dependency to this table, also add a
forbidden symbol pattern for it to
`.github/scripts/check_no_private_symbols.py` so the "Symbol leak check"
CI step will fail loudly if its symbols escape the public ABI of
`liblivekit`.

## Testing

Tests are under `src/tests/` using Google Test:

```
./build.sh debug-tests
cd build-debug && ctest
```

Integration tests (`src/tests/integration/`) cover: room connections, callbacks, data tracks, RPC, logging, audio processing, and the subscription thread dispatcher.

When adding new client facing functionality, add a new test case to the existing test suite.
When adding new client facing functionality, add benchmarking to understand the limitations of the new functionality.

## General C++ Development

- Do not use dynamic memory allocation after initialization
- Keep each function short (roughly ≤ 60 lines)
- Declare all data objects at the smallest possible level of scope
- Each calling function must check the return value of nonvoid functions, and each called function must check the validity of all parameters provided by the caller

## Common Pitfalls

- A `Room` with `auto_subscribe = false` will never receive remote audio/video frames — this is almost never what you want.
- A single participant cannot subscribe to its own tracks as "remote." Testing sender/receiver requires two separate room connections with distinct identities.
- macOS dylibs require `install_name_tool` fixups for `@rpath` — the CMakeLists.txt handles this automatically. Do not manually adjust RPATH unless you understand the implications.
- When consuming the installed SDK, use `-DLIVEKIT_LOCAL_SDK_DIR=/path/to/sdk` to point cmake at a local install instead of downloading a release tarball.

### CI Workflow Structure

`ci.yml` is the PR-review aggregator. It computes path changes once with
`dorny/paths-filter` and conditionally calls reusable workflows for the
expensive stages. Manual `workflow_dispatch` runs intentionally opt back into
all filtered stages; normal pull requests and pushes use the path filters.

- `.github/workflows/ci.yml` — Top-level PR/push/manual aggregator and path
  filter owner.
- `.github/workflows/builds.yml` — Reusable SDK and example-collection build
  matrix.
- `.github/workflows/tests.yml` — Reusable unit/integration test matrix.
- `.github/workflows/cpp-checks.yml` — Reusable `clang-format` and
  `clang-tidy` checks.
- `.github/workflows/generate-docs.yml` — Reusable Doxygen docs validation.
- `.github/workflows/rust-release-check.yml` — Reusable check that the pinned
 `client-sdk-rust` submodule commit maps to a published release. Gated by the
 `rust_submodule` path filter so it only runs on a submodule bump, runs in
 parallel, and is intentionally not a dependency of `builds`/`tests` so
 developer iteration against an unreleased Rust commit still gets build
 feedback.
- `.github/workflows/license_check.yml` — Cheap license check, run on every CI
  invocation.
- `.github/workflows/docker-images.yml` — Docker image build/publish workflow,
  outside PR-review aggregation.
- `.github/workflows/docker-validate.yml` — Docker image validation workflow,
  outside PR-review aggregation.

When adding or renaming files that affect a CI stage, update the matching
`ci.yml` `changes` filter in the same PR. For example, new build scripts,
CMake files, package manifests, or reusable build workflows should be added to
the `builds` filter; test-only helpers to `tests`; formatting/static-analysis
configuration (including `cpp-tools` submodule bumps) to `cpp_checks`; and docs
generation inputs to `docs`.

Keep broad agent guidance files such as `AGENTS.md` out of the expensive
`builds`, `tests`, `cpp_checks`, and `docs` filters unless they start affecting
generated docs or build artifacts. An `AGENTS.md`-only change should not trigger
those stages; only the always-on cheap checks should run.
