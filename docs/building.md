# Building

This document covers everything you need to build the LiveKit C++ SDK from
source: prerequisites, cloning the repository, the build scripts, and advanced
CMake/vcpkg flows.

## Prerequisites

### Core Tools

- **CMake** ≥ 3.25
- **Rust / Cargo** — latest stable toolchain (for building the Rust FFI layer).
  Install via [rustup](https://rustup.rs/).
- **Git/Submodules/LFS** — required to clone submodules and to pull test assets.

### Compilers

| Platform | Compiler | Package manager |
|----------|----------|-----------------|
| Linux    | Clang 21+ with libclang (See [here](#clang-compiler-usage-on-linux) for rationale) | `apt` |
| macOS    | Xcode 12+ Apple Clang (macOS 12.3+ for ScreenCaptureKit) | Homebrew |
| Windows  | Visual Studio 2019+ (MSBuild or Ninja) | vcpkg (see below) |

### Dependencies

These dependencies are linked by the SDK, used directly by its production C++
sources, or support optional repository targets.

| Dependency | Platform | Version | Source | How it is used |
|------------|----------|---------|--------|----------------|
| `client-sdk-rust` / `livekit_ffi` | All | `livekit_ffi` 0.13.3; pinned commit | Git submodule | Owns WebRTC, networking, media, and most business logic. The C++ SDK calls its C ABI and exchanges Protobuf FFI requests, responses, and events. Its shared library ships beside `liblivekit`. |
| Protobuf | All | 25.3 vendored; 5.29.5+ on Windows | Vendored / vcpkg | Generates and implements the FFI message types used throughout the C++ wrapper for rooms, tracks, streams, frames, statistics, RPC, encryption, and capture. |
| Abseil | All | 20240116.2 vendored; vcpkg-managed on Windows | Vendored / vcpkg | Supports the Protobuf build. Production C++ sources do not call Abseil APIs directly. |
| spdlog | All | 1.15.1 vendored; vcpkg-managed on Windows | Vendored / vcpkg | Implements the internal `LK_LOG_*` macros, default stderr logger, log-level filtering, and the public log callback adapter. |
| nlohmann/json | All | 3.12.0 vendored; vcpkg-managed on Windows | Vendored / vcpkg | Parses token-endpoint JSON responses and JWT payload claims. |
| Chromium trace-event code | All | Pinned source snapshot | Vendored source | Implements trace-event macros used by the production tracing API. Benchmark utilities consume the resulting Chromium-format traces, but Chromium is not a benchmark library dependency. |
| libcurl | Linux | System-provided | System library | Implements HTTP requests made by `TokenSource`. |
| OpenSSL | Linux | System-provided | System library | Linked into `liblivekit`. Current production C++ sources do not call OpenSSL APIs directly. |
| libcurl | Mac | System-provided | System library | Implements HTTP requests made by `TokenSource`. |
| Apple system frameworks | Mac | OS-provided | System frameworks | Supplies the audio, video, graphics, security, UI, and ScreenCaptureKit frameworks required by the media stack. |
| WinHTTP | Windows | OS-provided | System library | Implements HTTP requests made by `TokenSource`. |

### Capture Dependencies

| Dependency | Platform | Version | Source | How it is used |
|------------|----------|---------|--------|----------------|
| GStreamer | All | Platform-provided | System library | Used by the Rust `capture-gstreamer` backend. The C++ capture API reaches it through the FFI; GStreamer headers are not exposed publicly. |

### Test Dependencies

| Dependency | Platform | Version | Source | How it is used |
|------------|----------|---------|--------|----------------|
| GoogleTest | All | 1.14.0 | Vendored | Implements unit, integration, and stress test executables. It is not linked into SDK bundles. |

<a id="additional-capture-dependencies"></a>

## Installation

This section outlines the recommended dependency/environmental setup for
each supported platform.

### macOS

```bash
brew install cmake ninja protobuf abseil rust
```

### Linux

```bash
sudo apt update && sudo apt install -y \
  build-essential cmake ninja-build pkg-config git git-lfs curl \
  libglib2.0-dev libssl-dev libcurl4-openssl-dev \
  wget ca-certificates gnupg lsb-release

# Ubuntu 22.04 provides CMake 3.22. Install CMake 3.25+ separately
# (for example, from https://apt.kitware.com/) before building.

# Ubuntu 22.04 ships clang 14 and Ubuntu 24.04 ships clang 18. Install
# clang 21 and libclang from apt.llvm.org.
sudo install -m 0755 -d /etc/apt/keyrings
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
  | sudo tee /etc/apt/keyrings/llvm.asc >/dev/null
sudo chmod a+r /etc/apt/keyrings/llvm.asc
codename=$(lsb_release -cs)
echo "deb [signed-by=/etc/apt/keyrings/llvm.asc] http://apt.llvm.org/${codename}/ llvm-toolchain-${codename}-21 main" \
  | sudo tee /etc/apt/sources.list.d/llvm-21.list >/dev/null
sudo apt update
sudo apt install -y clang-21

# Install Rust if you don't already have it
curl https://sh.rustup.rs -sSf | sh

# After cargo installs, run this (or open a new terminal)
. "$HOME/.cargo/env"
```

If you plan to build the [example collection](https://github.com/livekit-examples/cpp-example-collection)
(SDL-based renderer + camera/mic capture), also install:

```bash
sudo apt install -y \
  libva-dev libdrm-dev libgbm-dev libx11-dev libgl1-mesa-dev \
  libxext-dev libxcomposite-dev libxdamage-dev libxfixes-dev \
  libxrandr-dev libxi-dev libxkbcommon-dev \
  libasound2-dev libpulse-dev \
  libwayland-dev libdecor-0-dev
```

### Windows

```powershell
# Set VCPKG_ROOT once and bootstrap vcpkg
git clone https://github.com/microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "$PWD\vcpkg"
```

CMake's vcpkg manifest mode (below) reads
`vcpkg.json` and installs the rest automatically the first time you configure.

## Clone the repository

The SDK uses recursive submodules for the [`Rust core`](https://github.com/livekit/rust-sdks), shared [C++ tooling](https://github.com/livekit/cpp-tools), and
the [C++ example collection](https://github.com/livekit-examples/cpp-example-collection).

```bash
# Option 1: clone with submodules in one step
git clone --recurse-submodules https://github.com/livekit/client-sdk-cpp.git

# Option 2: clone first, then initialize submodules
git clone https://github.com/livekit/client-sdk-cpp.git
cd client-sdk-cpp
git submodule update --init --recursive

# Pull the media asset required by the test suite:
git lfs pull
```

## Build scripts (recommended)

The repo ships with `build.sh` (Linux/macOS) and `build.cmd` (Windows) that
wrap the right CMake preset for your platform and pick sensible defaults.

**Linux/macOS:**
```bash
./build.sh release            # Build Release
./build.sh debug              # Build Debug
./build.sh release-examples   # Release + examples
./build.sh debug-examples     # Debug + examples
./build.sh release-tests      # Release + tests
./build.sh debug-tests        # Debug + tests
./build.sh release-all        # Release + tests + examples
./build.sh debug-all          # Debug + tests + examples
./build.sh build-all          # All Debug and Release variants above
./build.sh clean              # Clean CMake build artifacts + local-install
./build.sh clean-all          # Remove build dirs, local-install, and Rust target
```

**Windows:**
```powershell
.\build.cmd release
.\build.cmd debug
.\build.cmd release-examples
# ... same suffixes as build.sh
```

### GStreamer Installation (LiveKit Capture)

GStreamer is required only when the selected sources include `gstreamer`.
This includes `--capture` / `--capture=all` and release `-capture` bundles,
which enable all three sources. Standard SDK builds and builds selecting only
`pattern`, `clock`, or both do not require GStreamer.

For builds that include `gstreamer`, install GStreamer development libraries
at build time and GStreamer libraries at runtime. The SDK does not redistribute
these libraries or plugins. Install the runtime plugins required by your
pipeline too. Pattern and clock sources need a compatible GPU adapter at runtime.

**macOS:** Install both the official universal runtime and development packages
from the [GStreamer downloads page](https://gstreamer.freedesktop.org/download/#macos).
These packages support both x86_64 and arm64. Configure the framework paths:

```bash
export PATH="/Library/Frameworks/GStreamer.framework/Versions/1.0/bin:$PATH"
export PKG_CONFIG_PATH="/Library/Frameworks/GStreamer.framework/Versions/1.0/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
```

Use one GStreamer installation method per machine to keep libraries and plugins
consistent.

**Ubuntu / Debian:** Install the development packages and runtime plugins:

```bash
sudo apt install -y pkg-config libunwind-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav
```

**Windows:** Install the MSVC x86_64 runtime and development components from the
[GStreamer downloads page](https://gstreamer.freedesktop.org/download/#windows).
Add the installation's `bin` directory to `PATH` and its `lib/pkgconfig`
directory to `PKG_CONFIG_PATH`.

For GStreamer builds, development files must be discoverable through
`pkg-config`. You can verify
that they are available with:

```bash
pkg-config --modversion gstreamer-1.0 gstreamer-app-1.0
```

### Create an SDK bundle

To create an installable SDK bundle with public headers, runtime libraries, and
CMake package files, add `--bundle --prefix <install-dir>` to a build command:

**Linux/macOS:**

```bash
./build.sh release --bundle --prefix sdk-out/livekit-sdk
```

**Windows:**

```powershell
.\build.cmd release --bundle --prefix C:\path\to\livekit-sdk
```

The build scripts pass an explicit job count to `cmake --build --parallel`. Set
`CMAKE_BUILD_PARALLEL_LEVEL` to override the auto-detected logical CPU count.
Run `./build.sh help` for additional packaging and configuration options,
including `--archive`, `--archive-name`, `--version`, and `-G`.

## Advanced: CMake presets

For more control, drive CMake directly via the presets in
[CMakePresets.json](https://github.com/livekit/client-sdk-cpp/blob/main/CMakePresets.json):

```bash
# Linux
source scripts/setup-linux-toolchain.sh
cmake --preset linux-release
cmake --build --preset linux-release

# macOS
cmake --preset macos-release
cmake --build --preset macos-release

# Windows
cmake --preset windows-release
cmake --build --preset windows-release
```

Windows requires `VCPKG_ROOT` to be set:

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
```

## Advanced: Windows vcpkg manifest mode

vcpkg will automatically install all dependencies listed in
[vcpkg.json](https://github.com/livekit/client-sdk-cpp/blob/main/vcpkg.json) the first time you configure with its toolchain
file. This dependency path is supported only on Windows. Linux and macOS use
the vendored dependency paths described above.

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release

# With examples:
cmake -B build -S . `
  -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake `
  -DLIVEKIT_BUILD_EXAMPLES=ON
cmake --build build --config Release
```

## CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `LIVEKIT_BUILD_EXAMPLES` | OFF | Build example applications |
| `LIVEKIT_BUILD_TESTS` | OFF | Build test executables |
| `LIVEKIT_USE_SYSTEM_PROTOBUF` | OFF on Linux/macOS; ON on Windows | Select the platform dependency path; CMake forces the value |
| `LIVEKIT_LOG_LEVEL` | `TRACE` | Compile-time log threshold (see [logging.md](logging.md)) |
| `LIVEKIT_VERSION` | `0.0.0` | SDK version string baked into the binary; set with `--version` or CMake |
| `LIVEKIT_CAPTURE` | empty (`none`) | Capture sources for the Rust FFI (`all`, `none`, or a comma-separated list; see [Capture sources](#capture-sources)) |

## Build output

After a successful build:

```
build-release/
├── lib/
│   ├── liblivekit.{so,dylib}       # Main SDK shared library (Linux/macOS)
│   ├── liblivekit_ffi.{so,dylib}   # Rust FFI shared library (Linux/macOS)
│   └── livekit{,_ffi}.lib          # Import libraries (Windows)
├── include/                        # Public headers (auto-synced)
│   └── livekit/
├── generated/                      # Generated protobuf and FFI sources
├── compile_commands.json           # Compilation database
└── bin/                            # Tests/examples when enabled; DLLs on Windows
```

Release archives use the same layout: `include/`, `lib/`, and (on Windows)
`bin/`. The repository root also receives a `compile_commands.json` symlink.
Example builds use `local-install/` while configuring the example collection.
The exact build-tree layout can vary with the CMake generator.

## Integrating into your project

### Using CMake

```cmake
# Method 1: as a subdirectory
add_subdirectory(path/to/client-sdk-cpp)
target_link_libraries(your_target PRIVATE livekit)

# Method 2: find_package (after install)
find_package(LiveKit CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE LiveKit::livekit)
```

### Using prebuilt releases

The easiest way to consume the SDK without building from source is via
the [cpp-example-collection](https://github.com/livekit-examples/cpp-example-collection)
helper, which downloads a release tarball at CMake configure time:

```cmake
include(LiveKitSDK.cmake)  # pins or auto-resolves a release
```

See the example collection's
[`LiveKitSDK.cmake`](https://github.com/livekit-examples/cpp-example-collection/blob/main/cmake/LiveKitSDK.cmake)
for the full pattern.

### Manual linking and deployment

Prefer the CMake package above. It provides the correct include directory and
the `LiveKit::livekit` shared-library target. If you link a release archive
manually, add its `include/` directory and link only the main SDK library:

| Platform | Link-time file | Runtime files to deploy |
|----------|----------------|-------------------------|
| Windows | `lib/livekit.lib` | `bin/livekit.dll` and `bin/livekit_ffi.dll` beside the application executable |
| Linux | `lib/liblivekit.so` | `liblivekit.so` and `liblivekit_ffi.so` in the same runtime-library directory |
| macOS | `lib/liblivekit.dylib` | `liblivekit.dylib` and `liblivekit_ffi.dylib` in the same runtime-library directory |

Do not link the Rust FFI library directly: `livekit` already records it as a
shared-library dependency. On Linux, `liblivekit.so` uses a `$ORIGIN` runpath
to locate `liblivekit_ffi.so` next to it. On macOS, it uses `@loader_path` for
the same purpose. Your application must still be configured to find
`liblivekit` at runtime (for example, by using an appropriate executable
RPATH or platform packaging mechanism).

### Runtime dependencies of release artifacts

Official release archives bundle the two SDK libraries above. They do not
bundle, or dynamically depend on, Protobuf, Abseil, or OpenSSL. As of v1.7.0:

- **Windows:** the SDK DLLs depend on Windows system libraries and the
  Microsoft Visual C++ runtime. Install the supported Visual C++ Redistributable
  with your application when it is not already present. The archive contains no
  Protobuf or Abseil DLLs.
- **Linux:** `liblivekit.so` additionally depends on the system C++ runtime,
  glibc, and `libcurl.so.4`; `liblivekit_ffi.so` depends on the system C++
  runtime and glibc. Install the matching runtime packages for your target
  distribution.
- **macOS:** the SDK depends on system frameworks and the system `libcurl`;
  no Homebrew Protobuf, Abseil, or OpenSSL runtime is required.

These are release-artifact requirements, not source-build prerequisites. For
an updated audit of a specific release, inspect its binaries with `ldd`
(Linux), `otool -L` (macOS), or `dumpbin /dependents` (Windows).

## Troubleshooting

### Missing proto files or `client-sdk-rust` directory

Initialize submodules:
```bash
git submodule update --init --recursive
```

### Deprecated-declaration errors on Linux

Some WebRTC sources use deprecated declarations. If `./build.sh release`
errors with `-Werror=deprecated-declarations`, relax it for the build:

```bash
export CXXFLAGS="-Wno-deprecated-declarations"
export CFLAGS="-Wno-deprecated-declarations"
```

### Rust bindgen fails with "unable to find libclang"

On Linux, install `clang-21` as shown above. Rust bindgen uses libclang to
generate the libyuv bindings; this is separate from the Clang 21 compiler
requirement imposed by libwebrtc's hermetic libc++. `build.sh` accepts both
versioned libraries such as `libclang-21.so.1` and an unversioned
`libclang.so`. For a custom installation, set `LLVM_ROOT` or point
`LIBCLANG_PATH` at its library directory. On macOS, install the Xcode Command
Line Tools; `build.sh` selects Xcode Apple Clang with `xcrun`.

### Rust code recompiles after C++ edits

This was a historical issue; Rust only recompiles now when Rust source files
change or the Rust library is missing.

### Cannot find Protobuf or other dependencies

On Linux and macOS, Protobuf, Abseil, spdlog, and nlohmann/json are fetched
while CMake configures the project. Check network access and verify that all
recursive submodules are initialized:

```bash
git submodule update --init --recursive
```

On Windows, make sure `VCPKG_ROOT` is set and pass the vcpkg toolchain file as
shown above.

### `clang-tidy` on Windows

Not currently supported via our scripts — the Visual Studio (MSBuild) CMake
generator doesn't produce `compile_commands.json`. The Ninja generator does;
see [tools.md](tools.md).

Or clean one configured build tree with its CMake targets:

```bash
cmake --build build-release --target clean              # CMake artifacts
cmake --build build-release --target cargo_clean        # Rust artifacts
cmake --build build-release --target clean_generated    # Generated protobuf files
cmake --build build-release --target clean_all          # Full clean; removes build-release
```

Replace `build-release` with `build-debug` for a Debug configuration.

## Cargo output paths on Windows

The SDK places Rust FFI outputs in `client-sdk-rust/target/cpp-<feature-hash>/`.
Deep checkout paths can make WebRTC header paths exceed the Windows path limit.
Set `CARGO_TARGET_DIR` to a short absolute path before building, for example
`set CARGO_TARGET_DIR=C:\lk-rust` in Command Prompt. The SDK appends the
feature directory so different source selections have separate outputs.
Windows CI uses this setting for builds, tests, and release bundles.
`clean-all` removes the default Rust target directory; remove a custom root
separately when it is no longer needed.

<a id="capture-sources"></a>

## Capture sources

Capture is disabled by default. Select sources with `-DLIVEKIT_CAPTURE=<value>`
or `--capture[=list]` / `--no-capture`.

Available sources (comma-separated; each maps to `capture-*` on `livekit-ffi`):

| Source | Description |
|--------|-------------|
| `gstreamer` | GStreamer pipelines (needs GStreamer at build and runtime) |
| `pattern` | GPU test pattern |
| `clock` | GPU wall clock |

Special values: `none` or empty (disabled), `all` (every source).
`LIVEKIT_CAPTURE_ENABLED` is 1 when any source is selected; installed
`share/livekit/build-info.json` also records `cargo_features`.

```bash
./build.sh release --capture --bundle --prefix sdk-out/livekit-sdk-capture
./build.sh release --capture=pattern,clock --bundle --prefix sdk-out/livekit-sdk-pixel
./build.sh release --capture=gstreamer --bundle --prefix sdk-out/livekit-sdk-gstreamer
```

On Windows, use `build.cmd release --capture --bundle --prefix sdk-out\livekit-sdk-capture`.
Windows also accepts source lists, for example `build.cmd release --capture=pattern,clock`.
Custom CMake configurations use `LIVEKIT_CAPTURE=all` or a source list such as
`LIVEKIT_CAPTURE=pattern,clock`.

The build scripts set `LIVEKIT_CAPTURE`. The generated numeric
`LIVEKIT_CAPTURE_ENABLED` macro (0 or 1) supports `#if` checks in consumers.
It indicates whether any source was compiled in; it does not identify which
sources are available. Check `cargo_features` in `share/livekit/build-info.json`
for the selected backends. Creating a source omitted from the SDK build fails
through the returned future with `std::runtime_error`.
Capture-enabled bundles install the public API under `include/livekit/capture/`;
standard bundles omit that directory. Use `<livekit/capture/capture_source.h>`
for direct inclusion. The source-tree header also rejects use in a standard
build. `share/livekit/build-info.json` in an installed bundle describes the same
build features; neither value tests whether a GPU or a requested GStreamer plugin
is available at runtime.

Release downloads with `-capture` in their names include these features, for
example `livekit-sdk-macos-arm64-capture-<version>.tar.gz`. Standard release
bundles have capture disabled.

See [Additional capture dependencies](#additional-capture-dependencies) before
building or running a capture-enabled SDK.

Rust artifacts are stored under `client-sdk-rust/target/cpp-<feature-hash>/`,
with separate directories for each normalized source selection. Generated
`build.h` files live in each CMake build directory, so feature metadata follows
the installed SDK instead of another configuration's most recent build.

See [Capture sources](capture-sources.md) for dependencies and usage.

## Support

- GitHub Issues: <https://github.com/livekit/client-sdk-cpp/issues>
- LiveKit Docs: <https://docs.livekit.io/>

## Appendix

### Clang compiler usage on Linux

The SDK compiler requirement moved from GCC to Clang 21 when the core Rust SDK
[upgraded to WebRTC M150](https://github.com/livekit/rust-sdks/commit/415f05512b3b5cb417d21630e485c032c21d71b8).
LiveKit's M150 artifact is built with Chromium's hermetic libc++ and ships
those same headers for the Rust `webrtc-sys` C++ bridge. Chromium built that
artifact with Clang 23, while the shipped libc++ headers declare Clang 21 as
their minimum supported compiler. Older Clang versions fail on newer builtins
used by those headers. GCC is also unsuitable for this artifact because it
does not preserve the required libc++ `trivial_abi` calling conventions.

The SDK also heavily used Clang tooling for formatting and tidy code quality
checks before this WebRTC update, so a Clang dependency was not avoided before
the migration off GCC.

#### Avoiding Mixing Clang Versions

Stable Linux distributions generally freeze a compiler major version for the
life of a release. For example, Ubuntu 22.04 provides Clang 14, Ubuntu 24.04
provides Clang 18, Debian 12 provides Clang 14, and Debian 13 provides Clang
19. Those versions remain useful and supported by their distributions, but
they predate the LLVM toolchain used to produce the current libwebrtc
artifact. The apt.llvm.org repository above supplies a newer, versioned
toolchain without replacing the distribution compiler.

`build.sh` sources `scripts/setup-linux-toolchain.sh`, which locates the right
Clang environment paths and exports `CC`, `CXX`, and `LIBCLANG_PATH` for CMake
and Cargo. Within `build.sh`, those settings apply only to the build and its
child processes. Direct CMake users can source the same setup script and those
environment settings remain in the current shell. Neither usage modifies the
system-wide compiler configuration.

To use a custom LLVM installation instead, set
`LLVM_ROOT` to its prefix (for example, `/opt/llvm-21.1.8`).