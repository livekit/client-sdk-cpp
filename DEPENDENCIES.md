# LiveKit C++ SDK - System Dependencies

This file lists all system-level dependencies required to link against the LiveKit C++ SDK libraries.

## Overview

The LiveKit SDK consists of two libraries:
- `livekit.lib` / `liblivekit.a` - Main SDK static library
- `livekit_ffi.dll` / `liblivekit_ffi.so` / `liblivekit_ffi.dylib` - Rust FFI dynamic library

## Distribution Model

The SDK uses different distribution strategies per platform:

### Windows (Complete Package)
✅ **Ready to use** - All dependencies included:
- `livekit.lib` - Main SDK static library
- `livekit_ffi.dll` + `livekit_ffi.dll.lib` - Rust FFI dynamic library
- `libprotobuf.dll` + `libprotobuf.lib` - Protocol Buffers runtime
- `abseil_dll.dll` + `abseil_dll.lib` - Abseil C++ library

**User action**: Copy all DLLs alongside your executable. No additional installation required.

### Linux (Minimal Package)
⚠️ **Requires system dependencies**:
- `liblivekit.a` - Main SDK static library (included)
- `liblivekit_ffi.so` - Rust FFI dynamic library (included, **must be placed alongside your executable**)
- `libprotobuf` - Must install via `apt install libprotobuf-dev`
- `libssl` - Must install via `apt install libssl-dev`
- `libabsl` - Only if built with Protobuf 6.0+: `apt install libabsl-dev`

**User action**: Install required packages and copy `liblivekit_ffi.so` to your executable directory.

### macOS (Minimal Package)
⚠️ **Requires system dependencies**:
- `liblivekit.a` - Main SDK static library (included)
- `liblivekit_ffi.dylib` - Rust FFI dynamic library (included, **must be placed alongside your executable**)
- `protobuf` - Must install via `brew install protobuf`
- `abseil` - Only if built with Protobuf 6.0+: `brew install abseil`

**User action**: Install required packages and copy `liblivekit_ffi.dylib` to your executable directory.

---

## Windows Dependencies

When linking on Windows, you must add these system libraries:

```cmake
target_link_libraries(your_app PRIVATE
    # LiveKit static libraries
    livekit
    livekit_ffi
    
    # Windows system libraries (REQUIRED)
    ntdll          # NT kernel interface
    userenv        # User environment functions
    winmm          # Windows multimedia
    iphlpapi       # IP Helper API
    msdmo          # DirectX Media Objects
    dmoguids       # DMO GUIDs
    wmcodecdspuuid # Windows Media codec DSP UUIDs
    ws2_32         # Winsock 2
    secur32        # Security Support Provider Interface
    bcrypt         # Cryptography API
    crypt32        # Cryptography API (certificates)
)
```

### Visual Studio Project Settings
If using Visual Studio directly, add to **Linker → Input → Additional Dependencies**:
```
ntdll.lib;userenv.lib;winmm.lib;iphlpapi.lib;msdmo.lib;dmoguids.lib;wmcodecdspuuid.lib;ws2_32.lib;secur32.lib;bcrypt.lib;crypt32.lib
```

---

## macOS Dependencies

When linking on macOS, you must add these system frameworks:

```cmake
find_library(FW_COREAUDIO      CoreAudio      REQUIRED)
find_library(FW_AUDIOTOOLBOX   AudioToolbox   REQUIRED)
find_library(FW_COREFOUNDATION CoreFoundation REQUIRED)
find_library(FW_SECURITY       Security       REQUIRED)
find_library(FW_COREGRAPHICS   CoreGraphics   REQUIRED)
find_library(FW_COREMEDIA      CoreMedia      REQUIRED)
find_library(FW_VIDEOTOOLBOX   VideoToolbox   REQUIRED)
find_library(FW_AVFOUNDATION   AVFoundation   REQUIRED)
find_library(FW_COREVIDEO      CoreVideo      REQUIRED)
find_library(FW_FOUNDATION     Foundation     REQUIRED)
find_library(FW_APPKIT         AppKit         REQUIRED)
find_library(FW_QUARTZCORE     QuartzCore     REQUIRED)
find_library(FW_OPENGL         OpenGL         REQUIRED)
find_library(FW_IOSURFACE      IOSurface      REQUIRED)
find_library(FW_METAL          Metal          REQUIRED)
find_library(FW_METALKIT       MetalKit       REQUIRED)
find_library(FW_SCREENCAPTUREKIT ScreenCaptureKit REQUIRED)

target_link_libraries(your_app PRIVATE
    # LiveKit static libraries
    livekit
    livekit_ffi
    
    # macOS frameworks (REQUIRED)
    ${FW_COREAUDIO}
    ${FW_AUDIOTOOLBOX}
    ${FW_COREFOUNDATION}
    ${FW_SECURITY}
    ${FW_COREGRAPHICS}
    ${FW_COREMEDIA}
    ${FW_VIDEOTOOLBOX}
    ${FW_AVFOUNDATION}
    ${FW_COREVIDEO}
    ${FW_FOUNDATION}
    ${FW_APPKIT}
    ${FW_QUARTZCORE}
    ${FW_OPENGL}
    ${FW_IOSURFACE}
    ${FW_METAL}
    ${FW_METALKIT}
    ${FW_SCREENCAPTUREKIT}
)

target_link_options(your_app PRIVATE "LINKER:-ObjC")
```

### Xcode Project Settings
Add to **Build Phases → Link Binary With Libraries**:
- CoreAudio.framework
- AudioToolbox.framework
- CoreFoundation.framework
- Security.framework
- CoreGraphics.framework
- CoreMedia.framework
- VideoToolbox.framework
- AVFoundation.framework
- CoreVideo.framework
- Foundation.framework
- AppKit.framework
- QuartzCore.framework
- OpenGL.framework
- IOSurface.framework
- Metal.framework
- MetalKit.framework
- ScreenCaptureKit.framework

Also add `-ObjC` to **Other Linker Flags**.

---

## Linux Dependencies

### Required System Packages

First, install the required development packages:

```bash
# Ubuntu/Debian
sudo apt install libprotobuf-dev protobuf-compiler libabsl-dev libssl-dev

# Fedora/RHEL
sudo dnf install protobuf-devel abseil-cpp-devel openssl-devel
```

### CMake Configuration

```cmake
find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED)

target_link_libraries(your_app PRIVATE
    # LiveKit static libraries
    livekit
    livekit_ffi
    
    # Protobuf (REQUIRED)
    protobuf::libprotobuf
    
    # Linux system libraries (REQUIRED)
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
    dl
)

# NOTE: If using Protobuf 6.0+, you also need to link Abseil:
# find_package(absl REQUIRED)
# target_link_libraries(your_app PRIVATE absl::log absl::strings absl::base)
```

### Manual Linking (Makefile/gcc)
```bash
g++ your_app.cpp \
    -I/path/to/livekit/include \
    -L/path/to/livekit/lib \
    -llivekit -llivekit_ffi \
    -lprotobuf -lssl -lcrypto -lpthread -ldl
```

---

## Common Linking Errors

### Windows
```
error LNK2019: unresolved external symbol __imp_WSAStartup
```
**Solution**: Add `ws2_32.lib` to your linker dependencies.

```
error LNK2019: unresolved external symbol BCryptGenRandom
```
**Solution**: Add `bcrypt.lib` to your linker dependencies.

### macOS
```
Undefined symbols for architecture x86_64:
  "_AudioObjectGetPropertyData", referenced from:
```
**Solution**: Link against `CoreAudio.framework`.

### Linux
```
undefined reference to `SSL_CTX_new'
```
**Solution**: Install OpenSSL development package and link against it:
```bash
sudo apt install libssl-dev
```

---

## Quick Reference

| Platform | System Libraries Count | Key Dependencies |
|----------|------------------------|------------------|
| Windows  | 11 libraries           | ws2_32, bcrypt, secur32 |
| macOS    | 17 frameworks          | CoreAudio, VideoToolbox, ScreenCaptureKit |
| Linux    | 2+ libraries           | OpenSSL (ssl, crypto) |

---

## Verification

To verify you have all dependencies linked correctly:

1. **Build your application**
2. **Check for linker errors** - any `unresolved external symbol` or `undefined reference` indicates a missing system library
3. **Run the application** - if it crashes immediately, you may be missing runtime dependencies

---

## Need Help?

- See `README_BUILD.md` for complete build instructions
- Check `CMakeLists.txt` lines 287-381 for the exact CMake configuration
- Open an issue at: https://github.com/livekit/client-sdk-cpp/issues
