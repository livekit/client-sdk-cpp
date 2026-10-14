# Building

This document covers everything you need to build the LiveKit C++ SDK from
source: prerequisites, cloning the repository, the build scripts, and advanced
CMake/vcpkg flows.

## Prerequisites

### Common to all platforms

- **CMake** ≥ 3.20
- **Rust / Cargo** — latest stable toolchain (for building the Rust FFI layer).
  Install via [rustup](https://rustup.rs/).
- **Git LFS** — required for examples that pull test media assets.
- **Protobuf** ≥ 5.29
- **Abseil** — always required (used by Protobuf 5.x+)

### Platform-specific toolchains

| Platform | Compiler | Package manager |
|----------|----------|-----------------|
| Windows  | Visual Studio 2019+ (MSBuild or Ninja) | vcpkg (see below) |
| Linux    | GCC 9+ or Clang 10+ | `apt` / `dnf` (or vcpkg) |
| macOS    | Xcode 12+ (macOS 12.3+ for ScreenCaptureKit) | Homebrew (or vcpkg) |

## Clone the repository

The SDK depends on the [`client-sdk-rust`](https://github.com/livekit/rust-sdks)
submodule (recursive), so always clone with submodules:

```bash
# Option 1: clone with submodules in one step
git clone --recurse-submodules https://github.com/livekit/client-sdk-cpp.git

# Option 2: clone first, then initialize submodules
git clone https://github.com/livekit/client-sdk-cpp.git
cd client-sdk-cpp
git submodule update --init --recursive

# Pull Git LFS assets if you want to run the integration tests:
git lfs pull
```

## Recommended setup

These are the exact packages our CI uses. They will also work for examples.

### macOS

```bash
brew install cmake ninja protobuf abseil rust
```

### Ubuntu / Debian

```bash
sudo apt update && sudo apt install -y \
  build-essential cmake ninja-build pkg-config \
  llvm-dev libclang-dev clang \
  libprotobuf-dev protobuf-compiler libabsl-dev \
  libssl-dev

# Install Rust if you don't already have it
curl https://sh.rustup.rs -sSf | sh
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
./build.sh clean              # Clean CMake build artifacts + local-install
./build.sh clean-all          # Deep clean (C++ + Rust + local-install + generated files)
```

**Windows:**
```powershell
.\build.cmd release
.\build.cmd debug
.\build.cmd release-examples
# ... same suffixes as build.sh
```

### Create an SDK bundle

To create an installable SDK bundle with public headers, runtime libraries, and
CMake package files, add `--bundle --prefix <install-dir>` to a build command:

**Linux/Mac:**

```bash
./build.sh release --bundle --prefix sdk-out/livekit-sdk
```

**Windows:**

```powershell
.\build.cmd release --bundle --prefix C:\path\to\livekit-sdk
```

The build scripts pass an explicit job count to `cmake --build --parallel`. Set
`CMAKE_BUILD_PARALLEL_LEVEL` to override the auto-detected logical CPU count.

## Advanced: CMake presets

For more control, drive CMake directly via the presets in
[CMakePresets.json](https://github.com/livekit/client-sdk-cpp/blob/main/CMakePresets.json):

```bash
# Linux
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

## Advanced: vcpkg manifest mode

vcpkg will automatically install all dependencies listed in
[vcpkg.json](https://github.com/livekit/client-sdk-cpp/blob/main/vcpkg.json) the first time you configure with its toolchain
file.

**Windows:**
```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release

# With examples:
cmake -B build -S . `
  -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake `
  -DLIVEKIT_BUILD_EXAMPLES=ON
cmake --build build --config Release
```

**Linux/macOS:**
```bash
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build

# With examples:
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIVEKIT_BUILD_EXAMPLES=ON
cmake --build build
```

## CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `LIVEKIT_BUILD_EXAMPLES` | OFF | Build example applications |
| `LIVEKIT_USE_SYSTEM_PROTOBUF` | OFF | Use system Protobuf instead of vcpkg's |
| `LIVEKIT_LOG_LEVEL` | `TRACE` | Compile-time log threshold (see [logging.md](logging.md)) |
| `LIVEKIT_VERSION` | repo-derived | SDK version string baked into the binary |
| `LIVEKIT_BUILD_CAPTURE` | OFF | Enable GStreamer, pattern, and clock capture sources in the Rust FFI |

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
└── bin/
    └── livekit{,_ffi}.dll          # SDK DLLs (Windows)
```

Release archives use the same layout: `include/`, `lib/`, and (on Windows)
`bin/`. The exact build-tree layout can vary with the CMake generator.

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

Newer GCC versions (12+) are stricter with the WebRTC legacy code in the
Rust submodule. If `./build.sh release` errors with `-Werror=deprecated-declarations`,
relax it for the build:

```bash
export CXXFLAGS="-Wno-deprecated-declarations"
export CFLAGS="-Wno-deprecated-declarations"
```

### Rust bindgen fails with "unable to find libclang"

Install `libclang-dev` (Ubuntu) or `llvm` (macOS Homebrew). bindgen normally
discovers libclang from the system paths once `libclang-dev` is installed; if
not, point `LIBCLANG_PATH` at your LLVM's `lib` directory (e.g.
`/usr/lib/llvm-18/lib` on Ubuntu 24.04).

### Rust code recompiles after C++ edits

This was a historical issue; Rust only recompiles now when Rust source files
change or the Rust library is missing.

### Cannot find Protobuf or other dependencies

Make sure you're passing the vcpkg toolchain file:
```bash
-DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
```

### `clang-tidy` on Windows

Not currently supported via our scripts — the Visual Studio (MSBuild) CMake
generator doesn't produce `compile_commands.json`. The Ninja generator does;
see [tools.md](tools.md).

### How do I deep-clean?

```bash
./build.sh clean-all         # C++ + Rust + local-install + generated files
```

Or via CMake targets:

```bash
cmake --build build --target clean              # CMake artifacts
cmake --build build --target cargo_clean        # Rust artifacts
cmake --build build --target clean_generated    # Generated protobuf headers
cmake --build build --target clean_all          # Full clean
```

## Cargo output paths on Windows

The SDK places Rust FFI outputs in `client-sdk-rust/target/cpp-<feature-hash>/`.
Deep checkout paths can make WebRTC header paths exceed the Windows path limit.
Set `CARGO_TARGET_DIR` to a short absolute path before building, for example
`set CARGO_TARGET_DIR=C:\lk-rust` in Command Prompt. The SDK appends the
feature directory so capture-enabled and disabled outputs remain separate.
Windows CI uses this setting for builds, tests, and release bundles.
`clean-all` removes the default Rust target directory; remove a custom root
separately when it is no longer needed.

## Capture sources

Capture is disabled by default for every repository preset. Use `--capture`
with any build command to enable all three sources (GStreamer, GPU pattern, and
GPU wall clock), or `--no-capture` to disable them explicitly:

```bash
./build.sh release --capture --bundle --prefix sdk-out/livekit-sdk-capture
./build.sh debug-tests
```

On Windows, use `build.cmd release --capture --bundle --prefix sdk-out\livekit-sdk-capture`.
Custom CMake configurations can set `LIVEKIT_BUILD_CAPTURE=ON`. The build
enables `capture-gstreamer`, `capture-pattern`, and `capture-clock` in the
Rust FFI.

The build scripts set `LIVEKIT_BUILD_CAPTURE`. The generated numeric
`LIVEKIT_CAPTURE_ENABLED` macro (0 or 1) supports `#if` checks in consumers.
`share/livekit/build-info.json` in an installed bundle describes the same build
features; neither value tests whether a GPU or a requested GStreamer plugin is
available at runtime.

Release downloads with `-capture` in their names include these features, for
example `livekit-sdk-macos-arm64-capture-<version>.tar.gz`. Standard release
bundles have capture disabled.

Building a capture-enabled SDK requires GStreamer development libraries
accessible to `pkg-config`. On macOS, install both the official universal
runtime and development packages from the
[GStreamer downloads page](https://gstreamer.freedesktop.org/download/#macos).
Add `/Library/Frameworks/GStreamer.framework/Versions/1.0/bin` to the front of
`PATH` and `/Library/Frameworks/GStreamer.framework/Versions/1.0/lib/pkgconfig`
to the front of `PKG_CONFIG_PATH`. These packages support both x86_64 and arm64.
Use one GStreamer installation method per machine to keep its libraries and
plugins consistent. On Ubuntu, install `libgstreamer1.0-dev` and
`libgstreamer-plugins-base1.0-dev`, plus the runtime plugins your pipeline
uses. On Windows, install the MSVC runtime and development packages from the
[GStreamer downloads page](https://gstreamer.freedesktop.org/download/) and put
the installation's `bin` directory on `PATH` and `lib/pkgconfig` directory on
`PKG_CONFIG_PATH`.

Capture bundles require GStreamer at runtime, including when using pattern or
clock sources. The SDK does not redistribute GStreamer libraries or plugins.

CI and release jobs build both variants in one job per platform. The shared
`setup-sdk-deps` action installs base dependencies first. Each job builds and
validates the standard SDK before the `setup-capture` action installs GStreamer.
It then runs the build script with `--capture` in the same CMake build directory.
Unchanged C++ dependencies and generated protobuf outputs are reused. Both
bundles have separate install directories and artifact names, and both are
checked through the installed-SDK packaging tests. Release jobs also check
version metadata and exported symbols in both bundles.

Test jobs validate the standard installed SDK first, then enable capture and
run the full unit, integration, and optional stress suites once. Rust outputs
remain separate for each feature configuration; the shared Cargo cache stores
both configurations.

Coverage shares the dependency setup actions and builds only the instrumented
capture variant.

Rust artifacts are stored under `client-sdk-rust/target/cpp-<feature-hash>/`,
with separate directories for capture-enabled and disabled builds. Generated
`build.h` files live in each CMake build directory, so feature metadata follows
the installed SDK instead of another configuration's most recent build.

See [Capture sources](capture-sources.md) for dependencies and usage.

## Build in a Linux container

You can test a clean Linux build and its dependency tree from a macOS or Linux
host with `docker/linux.Dockerfile`. The container is a build check, not
a development environment. The image uses Ubuntu 24.04 with the same packages
as the Linux CI jobs. It also includes GStreamer for `--capture` builds and
the stable Rust toolchain.

The container bind-mounts the source paths that the build reads from the host
into `/work`. Changes on the host show up in the container immediately. The
`/work` directory itself is the `lk-cpp-work` volume, so build outputs such as
`build-*`, `local-install`, and `sdk-out` stay in the container. Host build
directories and `.git` data are not mounted.

1. From the repository root, build the image. The `client-sdk-rust` submodule
   must be checked out, because the image copies its clang install script.

   ```bash
   docker build -f docker/linux.Dockerfile -t livekit-cpp-build .
   ```

   To install gstreamer:
   ```bash
   docker build -f docker/linux.Dockerfile \
     --build-arg INSTALL_GSTREAMER=1 \
     -t livekit-cpp-build .
   ```

   The build context is the repository root. The file
   `docker/linux.Dockerfile.dockerignore` limits the context to the
   clang install script, so Docker does not send build outputs or `.git`.

2. From the repository root, start the container:

   ```bash
   docker run -d --name lk-cpp --network host \
     -v lk-cpp-work:/work \
     -v lk-cargo-target:/cargo-target \
     -v lk-cargo-registry:/opt/cargo/registry \
     -v "$(pwd)/build.sh:/work/build.sh" \
     -v "$(pwd)/CMakeLists.txt:/work/CMakeLists.txt" \
     -v "$(pwd)/CMakePresets.json:/work/CMakePresets.json" \
     -v "$(pwd)/build.h.in:/work/build.h.in" \
     -v "$(pwd)/.build-info.json.in:/work/.build-info.json.in" \
     -v "$(pwd)/cmake:/work/cmake" \
     -v "$(pwd)/include:/work/include" \
     -v "$(pwd)/src:/work/src" \
     -v "$(pwd)/scripts:/work/scripts" \
     -v "$(pwd)/client-sdk-rust:/work/client-sdk-rust" \
     -v lk-rust-source-target:/work/client-sdk-rust/target \
     -v "$(pwd)/cpp-example-collection:/work/cpp-example-collection" \
     livekit-cpp-build sleep infinity
   ```

3. Open a shell in the container:

   ```bash
   docker exec -it lk-cpp bash
   ```
For a fully clean build, run `./build.sh clean-all` in the container, or remove the volumes.

## Support

- GitHub Issues: <https://github.com/livekit/client-sdk-cpp/issues>
- LiveKit Docs: <https://docs.livekit.io/>
