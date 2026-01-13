# cmake/protobuf.cmake
#
# Windows: use vcpkg Protobuf (static-md triplet) + vcpkg protoc
# macOS/Linux: vendored Protobuf (static) + vendored Abseil + vendored protoc
#
# Exposes:
#   - Protobuf_PROTOC_EXECUTABLE
#   - Protobuf_INCLUDE_DIRS
#   - Target protobuf::libprotobuf (and optionally protobuf::libprotobuf-lite)
#   - Target protobuf::protoc (on vendored path; on Windows we may only have an executable)

include(FetchContent)

option(LIVEKIT_USE_SYSTEM_PROTOBUF "Use system-installed Protobuf instead of vendoring" OFF)

set(LIVEKIT_PROTOBUF_VERSION "25.3" CACHE STRING "Vendored Protobuf version")
set(LIVEKIT_ABSEIL_VERSION  "20240116.2" CACHE STRING "Vendored Abseil version")

# ---------------------------------------------------------------------------
# Windows path: prefer vcpkg static-md protobuf to avoid /MT vs /MD mismatches.
# This assumes you configure CMake with:
#   -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
#   -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
#
# (or set VCPKG_TARGET_TRIPLET in the environment before configuring)
# ---------------------------------------------------------------------------
if(WIN32 AND NOT LIVEKIT_USE_SYSTEM_PROTOBUF)
  # If the user forgot the triplet, fail fast with a helpful message.
  if(DEFINED VCPKG_TARGET_TRIPLET AND NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static-md")
    message(FATAL_ERROR
      "On Windows, LiveKit expects vcpkg triplet x64-windows-static-md for static protobuf + /MD.\n"
      "You have VCPKG_TARGET_TRIPLET='${VCPKG_TARGET_TRIPLET}'.\n"
      "Reconfigure with -DVCPKG_TARGET_TRIPLET=x64-windows-static-md."
    )
  elseif(NOT DEFINED VCPKG_TARGET_TRIPLET)
    message(WARNING
      "VCPKG_TARGET_TRIPLET is not set. On Windows you should configure with:\n"
      "  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md\n"
      "to get static protobuf built against /MD."
    )
  endif()

  # Ensure /MD for everything in this top-level build.
  if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)
  endif()

  # Use vcpkg's Protobuf package (CONFIG mode provides imported targets).
  # This should give protobuf::libprotobuf and protobuf::protoc.
  find_package(Protobuf CONFIG REQUIRED)

  # Prefer protoc target if available; else fall back to locating protoc.
  if(TARGET protobuf::protoc)
    set(Protobuf_PROTOC_EXECUTABLE "$<TARGET_FILE:protobuf::protoc>" CACHE STRING "protoc (vcpkg)" FORCE)
  elseif(DEFINED Protobuf_PROTOC_EXECUTABLE AND Protobuf_PROTOC_EXECUTABLE)
    # Some find modules populate this var
    set(Protobuf_PROTOC_EXECUTABLE "${Protobuf_PROTOC_EXECUTABLE}" CACHE STRING "protoc (vcpkg/module)" FORCE)
  else()
    find_program(Protobuf_PROTOC_EXECUTABLE NAMES protoc REQUIRED)
    set(Protobuf_PROTOC_EXECUTABLE "${Protobuf_PROTOC_EXECUTABLE}" CACHE STRING "protoc (found)" FORCE)
  endif()

  # Include dirs: prefer the imported target usage requirements.
  if(TARGET protobuf::libprotobuf)
    get_target_property(_pb_includes protobuf::libprotobuf INTERFACE_INCLUDE_DIRECTORIES)
  elseif(TARGET protobuf::protobuf) # some protobuf builds use protobuf::protobuf
    get_target_property(_pb_includes protobuf::protobuf INTERFACE_INCLUDE_DIRECTORIES)
  endif()
  if(NOT _pb_includes)
    # Best-effort fallback: Protobuf_INCLUDE_DIRS is commonly set by ProtobufConfig
    set(_pb_includes "${Protobuf_INCLUDE_DIRS}")
  endif()
  set(Protobuf_INCLUDE_DIRS "${_pb_includes}" CACHE STRING "Protobuf include dirs" FORCE)

  message(STATUS "Windows: using vcpkg Protobuf (expect triplet x64-windows-static-md)")
  message(STATUS "Windows: protoc = ${Protobuf_PROTOC_EXECUTABLE}")
  return()
endif()

# ---------------------------------------------------------------------------
# Optional "system protobuf" path (non-vcpkg use, or user wants system package)
# ---------------------------------------------------------------------------
if(LIVEKIT_USE_SYSTEM_PROTOBUF)
  find_package(Protobuf CONFIG QUIET)
  if(NOT Protobuf_FOUND)
    find_package(Protobuf REQUIRED)
  endif()

  if(NOT Protobuf_PROTOC_EXECUTABLE)
    find_program(Protobuf_PROTOC_EXECUTABLE NAMES protoc REQUIRED)
  endif()
  message(STATUS "Using system protoc: ${Protobuf_PROTOC_EXECUTABLE}")
  return()
endif()

# ---------------------------------------------------------------------------
# macOS/Linux path: vendored Abseil + vendored Protobuf (static) + vendored protoc
# ---------------------------------------------------------------------------

# ---- Abseil ----
FetchContent_Declare(
  livekit_abseil
  URL "https://github.com/abseil/abseil-cpp/archive/refs/tags/${LIVEKIT_ABSEIL_VERSION}.tar.gz"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# ---- Protobuf ----
FetchContent_Declare(
  livekit_protobuf
  URL "https://github.com/protocolbuffers/protobuf/releases/download/v${LIVEKIT_PROTOBUF_VERSION}/protobuf-${LIVEKIT_PROTOBUF_VERSION}.tar.gz"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Configure protobuf build: static libs, no tests/examples.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Disable installs/exports in subprojects (avoids export-set errors)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(utf8_range_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)

set(protobuf_ABSL_PROVIDER "package" CACHE STRING "" FORCE)

# Keep /MD for MSVC, though this branch is not Windows by default.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)
endif()

# Make abseil available first so protobuf can find absl:: targets.
FetchContent_MakeAvailable(livekit_abseil)

# Workaround for some abseil flags on Apple Silicon.
if(APPLE AND (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"))
  foreach(t
    absl_random_internal_randen_hwaes_impl
    absl_random_internal_randen_hwaes
  )
    if(TARGET ${t})
      foreach(prop COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS)
        get_target_property(_opts ${t} ${prop})
        if(_opts)
          list(FILTER _opts EXCLUDE REGEX "^-Xarch_x86_64$")
          list(FILTER _opts EXCLUDE REGEX "^-msse4\\.1$")
          list(FILTER _opts EXCLUDE REGEX "^-maes$")
          set_target_properties(${t} PROPERTIES ${prop} "${_opts}")
        endif()
      endforeach()
    endif()
  endforeach()
endif()

if(NOT TARGET absl::base)
  message(FATAL_ERROR "Abseil targets not found after FetchContent_MakeAvailable(livekit_abseil)")
endif()

# Now make protobuf available.
FetchContent_MakeAvailable(livekit_protobuf)

# Protobuf targets: modern protobuf exports protobuf::protoc etc.
if(TARGET protobuf::protoc)
  set(Protobuf_PROTOC_EXECUTABLE "$<TARGET_FILE:protobuf::protoc>" CACHE STRING "protoc (vendored)" FORCE)
elseif(TARGET protoc)
  set(Protobuf_PROTOC_EXECUTABLE "$<TARGET_FILE:protoc>" CACHE STRING "protoc (vendored)" FORCE)
else()
  message(FATAL_ERROR "Vendored protobuf did not create a protoc target")
endif()

# Prefer protobuf-lite (optional; keep libprotobuf around too)
if(TARGET protobuf::libprotobuf-lite)
  # ok
elseif(TARGET libprotobuf-lite)
  add_library(protobuf::libprotobuf-lite ALIAS libprotobuf-lite)
else()
  message(WARNING "Vendored protobuf did not create protobuf-lite target; continuing with libprotobuf only")
endif()

# Include dirs: prefer target usage; keep this var for your existing CMakeLists.
if(TARGET protobuf::libprotobuf)
  get_target_property(_pb_includes protobuf::libprotobuf INTERFACE_INCLUDE_DIRECTORIES)
endif()
if(NOT _pb_includes)
  set(_pb_includes "${livekit_protobuf_SOURCE_DIR}/src")
endif()
set(Protobuf_INCLUDE_DIRS "${_pb_includes}" CACHE STRING "Protobuf include dirs" FORCE)

message(STATUS "macOS/Linux: using vendored Protobuf v${LIVEKIT_PROTOBUF_VERSION}")
message(STATUS "macOS/Linux: vendored protoc: ${Protobuf_PROTOC_EXECUTABLE}")
