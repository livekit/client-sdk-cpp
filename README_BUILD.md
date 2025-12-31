# LiveKit C++ Client SDK - Build Guide

## Prerequisites

### Required Tools
1. **CMake** (>= 3.20)
2. **Rust and Cargo** - For building the Rust FFI layer
3. **C++ Compiler**
   - Windows: Visual Studio 2019 or later
   - Linux: GCC 9+ or Clang 10+
   - macOS: Xcode 12+

### Dependency Management

This project uses different dependency management strategies per platform:

| Platform | Package Manager | Dependencies |
|----------|-----------------|-------------|
| Windows  | vcpkg (bundled) | protobuf, abseil (DLLs included in distribution) |
| Linux    | apt/dnf | `libprotobuf-dev libabsl-dev libssl-dev` |
| macOS    | Homebrew | `protobuf abseil` |

#### Windows: Install vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "$(Get-Location)"
```

#### Linux: Install Dependencies

```bash
# Ubuntu/Debian
sudo apt update && sudo apt install -y \
    build-essential cmake ninja-build pkg-config \
    llvm-dev libclang-dev clang \
    libprotobuf-dev protobuf-compiler libabsl-dev \
    libssl-dev libva-dev libdrm-dev libgbm-dev libx11-dev libgl1-mesa-dev
```

#### macOS: Install Dependencies

```bash
brew install cmake ninja protobuf abseil
```

## Quick Start

### Method 1: Using Build Scripts (Recommended)

The project provides `build.cmd` (Windows) and `build.sh` (Linux/macOS) scripts for simplified building.

**Windows:**
```powershell
# Set vcpkg root (required for Windows)
$env:VCPKG_ROOT = "C:\path\to\vcpkg"

# Build Release version
.\build.cmd release

# Build Release with examples
.\build.cmd release-examples

# Build Debug version
.\build.cmd debug

# Build Debug with examples
.\build.cmd debug-examples

# Clean build artifacts
.\build.cmd clean

# Full clean (C++ + Rust + generated files)
.\build.cmd clean-all
```

**Linux:**
```bash
# Install system dependencies first (see Prerequisites above)

# Build Release version
./build.sh release

# Build Release with examples
./build.sh release-examples

# Build Debug version
./build.sh debug

# Build Debug with examples
./build.sh debug-examples

# Clean build artifacts
./build.sh clean

# Full clean
./build.sh clean-all
```

**macOS:**
```bash
# Install Homebrew dependencies first (see Prerequisites above)

# Build Release version
./build.sh release

# Build Release with examples
./build.sh release-examples

# Build Debug version
./build.sh debug

# Build Debug with examples
./build.sh debug-examples
```

#### Important Notes for Linux

Before building on Linux (especially Ubuntu/WSL), you may need to set environment variables to avoid common build errors.

**Set Build Environment Variables:**
```bash
# Suppress deprecated warnings from WebRTC (required for newer GCC versions)
export CXXFLAGS="-Wno-deprecated-declarations"
export CFLAGS="-Wno-deprecated-declarations"

# Required for Rust bindgen to find libclang
export LIBCLANG_PATH=/usr/lib/llvm-14/lib  # Adjust version as needed
```

**Common Issues:**

1. **Missing proto files or client-sdk-rust directory**
   - Solution: Initialize git submodules:
     ```bash
     git submodule update --init --recursive
     ```

2. **Deprecated declaration errors during compilation**
   - Cause: Newer GCC versions (12/13/14) are stricter with WebRTC legacy code
   - Solution: Set `CXXFLAGS` and `CFLAGS` as shown above

3. **Rust bindgen fails with "unable to find libclang"**
   - Cause: Rust bindgen cannot locate libclang library
   - Solution: Set `LIBCLANG_PATH` environment variable pointing to your LLVM installation

### Method 2: Using vcpkg Manifest Mode

vcpkg will automatically install all required dependencies based on `vcpkg.json`.

**Windows:**
```powershell
# Configure project
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release

# Optional: Build with examples
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake -DLIVEKIT_BUILD_EXAMPLES=ON -DVCPKG_MANIFEST_FEATURES=examples
cmake --build build --config Release
```

**Linux/macOS:**
```bash
# Configure project
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Optional: Build with examples
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release -DLIVEKIT_BUILD_EXAMPLES=ON -DVCPKG_MANIFEST_FEATURES=examples
cmake --build build
```

### Method 3: Manual Dependency Installation

If you prefer not to use vcpkg, you can install dependencies manually:

#### Required Dependencies
- **Protobuf** (>= 5.29)
  - Windows: `vcpkg install protobuf:x64-windows`
  - Linux: `sudo apt install libprotobuf-dev protobuf-compiler`
  - macOS: `brew install protobuf`

- **Abseil** (Required for Protobuf >= 6)
  - Windows: `vcpkg install abseil:x64-windows`
  - Linux: `sudo apt install libabsl-dev`
  - macOS: `brew install abseil`

- **OpenSSL** (Linux only)
  - Linux: `sudo apt install libssl-dev`

#### Build Command
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Build Output

After a successful build, you will find the following in the build directories:

```
build-release/                # Release build output
├── lib/
│   ├── windows-x64/         # Windows libraries
│   │   └── release/
│   ├── linux-x64/           # Linux libraries
│   └── macos-universal/     # macOS libraries
├── include/                 # Public headers (auto-synced)
│   └── livekit/
└── bin/                     # Executable files

build-debug/                  # Debug build output
├── lib/
│   ├── windows-x64/
│   │   └── debug/
│   ├── linux-x64/
│   └── macos-universal/
├── include/
└── bin/
```

## Integrating into Your Project

### Using CMake

```cmake
# Method 1: As a subdirectory
add_subdirectory(path/to/client-sdk-cpp)
target_link_libraries(your_target PRIVATE livekit)

# Method 2: Using find_package (requires prior installation)
find_package(livekit REQUIRED)
target_link_libraries(your_target PRIVATE livekit)
```

### Manual Linking

1. Add include path: `build/include`
2. Link static library: 
   - Windows: `build/lib/windows-x64/release/livekit.lib`
   - Linux: `build/lib/linux-x64/liblivekit.a`
   - macOS: `build/lib/macos-universal/liblivekit.a`
3. Link Rust FFI library:
   - Windows: `client-sdk-rust/target/release/livekit_ffi.lib`
   - Linux/macOS: `client-sdk-rust/target/release/liblivekit_ffi.a`
4. Link platform-specific system libraries

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

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `LIVEKIT_BUILD_EXAMPLES` | OFF | Build example applications |
| `LIVEKIT_VERSION` | "0.1.0" | SDK version number |
| `LIVEKIT_USE_VCPKG` | ON | Use vcpkg for dependency management |

## Troubleshooting

### Q: Rust code recompiles after modifying C++ code?
A: This has been fixed. Rust code only recompiles when Rust source files change or the Rust library doesn't exist.

### Q: Cannot find Protobuf or other dependencies?
A: Make sure you're using the correct CMake toolchain file:
```bash
-DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
```

### Q: Linking fails on Linux?
A: Ensure you have OpenSSL development packages installed:
```bash
sudo apt install libssl-dev
```

### Q: How to clean the build?
A: Use the CMake-provided clean targets:
```bash
# Clean CMake build artifacts
cmake --build build --target clean

# Clean Rust build artifacts
cmake --build build --target cargo_clean

# Clean generated protobuf files
cmake --build build --target clean_generated

# Complete clean (including deletion of build directory)
cmake --build build --target clean_all
```

## Example Applications

Example applications are located in the `examples/` directory:

- **SimpleRoom** - Basic room connection and audio/video handling
- **SimpleRpc** - RPC call examples
- **SimpleDataStream** - Data stream transmission examples

Please refer to the README in each example directory for more details.

## Platform-Specific Notes

### Windows
- Recommended: Visual Studio 2019 or later
- Architecture: x64

### macOS
- Requires macOS 12.3+ (ScreenCaptureKit support)
- Requires Xcode Command Line Tools

### Linux
- Tested on: Ubuntu 20.04+
- Requires complete development toolchain

## Support

- GitHub Issues: https://github.com/livekit/client-sdk-cpp/issues
- LiveKit Documentation: https://docs.livekit.io/

