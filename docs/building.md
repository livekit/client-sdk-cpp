# Building

This document covers everything you need to build the LiveKit C++ SDK from
source: prerequisites, cloning the repository, the build scripts, advanced
CMake/vcpkg flows, and Docker.

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

## Building with Docker

The Docker setup is split into a reusable base image (toolchain + system
deps) and an SDK image layered on top. **Tested on Linux only.**

```bash
docker build -t livekit-cpp-sdk-base . -f docker/Dockerfile.base
docker build --build-arg BASE_IMAGE=livekit-cpp-sdk-base \
  -t livekit-cpp-sdk . -f docker/Dockerfile.sdk
docker run -it --network host livekit-cpp-sdk:latest bash
```

If you're authoring your own Dockerfile, mirror the `ENV` block in
[docker/Dockerfile.base](https://github.com/livekit/client-sdk-cpp/blob/main/docker/Dockerfile.base):

```bash
export CC=$HOME/gcc-14/bin/gcc
export CXX=$HOME/gcc-14/bin/g++
export LD_LIBRARY_PATH=$HOME/gcc-14/lib64:$LD_LIBRARY_PATH
export PATH=$HOME/.cargo/bin:$PATH
export PATH=$HOME/cmake-3.31/bin:$PATH
```

## CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `LIVEKIT_BUILD_EXAMPLES` | OFF | Build example applications |
| `LIVEKIT_USE_SYSTEM_PROTOBUF` | OFF | Use system Protobuf instead of vcpkg's |
| `LIVEKIT_LOG_LEVEL` | `TRACE` | Compile-time log threshold (see [logging.md](logging.md)) |
| `LIVEKIT_VERSION` | repo-derived | SDK version string baked into the binary |

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

## Support

- GitHub Issues: <https://github.com/livekit/client-sdk-cpp/issues>
- LiveKit Docs: <https://docs.livekit.io/>
