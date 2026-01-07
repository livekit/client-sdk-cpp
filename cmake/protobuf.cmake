# cmake/protobuf.cmake
#
# Vendored Protobuf (static) + protobuf-lite + protoc for codegen.
# Also fetches Abseil because protobuf >= 22 commonly requires it.
#
# Exposes:
#   - Protobuf_PROTOC_EXECUTABLE  (generator expression: $<TARGET_FILE:protobuf::protoc>)
#   - Protobuf_INCLUDE_DIRS       (best-effort; prefer target include dirs)
#   - Target protobuf::libprotobuf-lite
#   - Target protobuf::protoc

include(FetchContent)

option(LIVEKIT_USE_SYSTEM_PROTOBUF "Use system-installed Protobuf instead of vendoring" OFF)

set(LIVEKIT_PROTOBUF_VERSION "25.3" CACHE STRING "Vendored Protobuf version")
set(LIVEKIT_ABSEIL_VERSION  "20240116.2" CACHE STRING "Vendored Abseil version")

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

# ---- Abseil (needed by protobuf on many versions) ----
# Fetch Abseil and make it available as CMake targets (absl::...)
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
# Build static only
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Disable installs/exports in subprojects (avoids export-set errors)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(utf8_range_ENABLE_INSTALL   OFF CACHE BOOL "" FORCE) 

set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)

set(protobuf_ABSL_PROVIDER "package" CACHE STRING "" FORCE)

# Make abseil available first so protobuf can find absl:: targets.
FetchContent_MakeAvailable(livekit_abseil)

# A workaround to remove the -Xarch_x86_64 / -msse4 flags that could fail the compilation.
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



# Some protobuf versions look for absl via find_package(absl CONFIG).
# The abseil project usually provides targets directly, but not a config package.
# To help protobuf, ensure absl targets exist (they should after MakeAvailable).
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

# Prefer protobuf-lite
if(TARGET protobuf::libprotobuf-lite)
  # ok
elseif(TARGET libprotobuf-lite)
  add_library(protobuf::libprotobuf-lite ALIAS libprotobuf-lite)
else()
  message(FATAL_ERROR "Vendored protobuf did not create protobuf-lite target")
endif()

# Include dirs: prefer target usage; keep this var for your existing CMakeLists.
get_target_property(_pb_includes protobuf::libprotobuf-lite INTERFACE_INCLUDE_DIRECTORIES)
if(NOT _pb_includes)
  # common fallback
  set(_pb_includes "${livekit_protobuf_SOURCE_DIR}/src")
endif()
set(Protobuf_INCLUDE_DIRS "${_pb_includes}" CACHE STRING "Protobuf include dirs" FORCE)

message(STATUS "Using vendored Protobuf v${LIVEKIT_PROTOBUF_VERSION}")
message(STATUS "Using vendored protoc: ${Protobuf_PROTOC_EXECUTABLE}")
