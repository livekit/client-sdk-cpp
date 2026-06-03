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
- **Protobuf** ≥ 5.29 — provides `protoc`; the SDK links against protobuf-lite.
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
  -DLIVEKIT_BUILD_EXAMPLES=ON -DVCPKG_MANIFEST_FEATURES=examples
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
  -DLIVEKIT_BUILD_EXAMPLES=ON -DVCPKG_MANIFEST_FEATURES=examples
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
| `LIVEKIT_USE_SYSTEM_PROTOBUF` | OFF | Use system Protobuf instead of the vendored package |
| `LIVEKIT_LOG_LEVEL` | `TRACE` | Compile-time log threshold (see [logging.md](logging.md)) |
| `LIVEKIT_VERSION` | repo-derived | SDK version string baked into the binary |

## Build output

After a successful build:

```
build-release/
├── lib/
│   ├── liblivekit.{a,lib}          # Main SDK static library
│   ├── liblivekit_ffi.{so,dylib}   # Rust FFI dynamic library
│   └── livekit_ffi.dll, *.lib      # (Windows) FFI DLL + import lib
├── include/                        # Public headers (auto-synced)
│   └── livekit/
└── bin/                            # Example/test executables
    └── liblivekit_ffi.{so,dylib}   # (Linux/macOS: copied for runtime)
```

## Integrating into your project

### Using CMake

```cmake
# Method 1: as a subdirectory
add_subdirectory(path/to/client-sdk-cpp)
target_link_libraries(your_target PRIVATE livekit)

# Method 2: find_package (after install)
find_package(livekit REQUIRED)
target_link_libraries(your_target PRIVATE livekit)
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

### Manual linking

1. Add include path: `build-release/include`
2. Link the static SDK library:
   - Windows: `build-release/lib/livekit.lib`
   - Linux: `build-release/lib/liblivekit.a`
   - macOS: `build-release/lib/liblivekit.a`
3. Link/deploy the Rust FFI dynamic library:
   - Windows: link `livekit_ffi.dll.lib`, deploy `livekit_ffi.dll` next to your `.exe`
   - Linux: deploy `liblivekit_ffi.so` next to your executable
   - macOS: deploy `liblivekit_ffi.dylib` next to your executable
4. Link platform system libraries (see below).

> **Important:** On Linux/macOS the FFI shared library must live next to the
> executable. RPATH is set to `$ORIGIN` (Linux) / `@loader_path` (macOS).

**Windows system libraries:**
```
ntdll userenv winmm iphlpapi msdmo dmoguids wmcodecdspuuid
ws2_32 secur32 bcrypt crypt32
```

**macOS frameworks:**
```
CoreAudio AudioToolbox CoreFoundation Security CoreGraphics
CoreMedia VideoToolbox AVFoundation CoreVideo Foundation
AppKit QuartzCore OpenGL IOSurface Metal MetalKit ScreenCaptureKit
```

**Linux libraries:**
```
OpenSSL::SSL OpenSSL::Crypto
```

### Runtime dependencies of prebuilt artifacts

Whether protobuf-lite / abseil / openssl need to be installed on the target
machine depends on how the SDK binary was built:

- **Windows** release artifacts use vcpkg triplet `x64-windows-static-md` —
  protobuf-lite and abseil are statically linked into the DLLs; no runtime install
  needed.
- **macOS** release artifacts (from our CI) do **not** dynamically depend on
  protobuf-lite / abseil / openssl. You can verify with `otool -L liblivekit.dylib`.
- **Linux** depends on packaging. Check with `ldd liblivekit_ffi.so`; if any
  of those are listed, install the corresponding `-dev` (build) or runtime
  package (`libprotobuf-lite32` / `libabsl` / `libssl3`) as appropriate.

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
