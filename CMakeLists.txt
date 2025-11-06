cmake_minimum_required(VERSION 4.1)
project(livekit LANGUAGES C CXX)

# ---- C++ standard ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---- Protobuf (FFI protos) ----
set(FFI_PROTO_DIR ${CMAKE_SOURCE_DIR}/client-sdk-rust/livekit-ffi/protocol)
set(FFI_PROTO_FILES
    ${FFI_PROTO_DIR}/handle.proto
    ${FFI_PROTO_DIR}/ffi.proto
    ${FFI_PROTO_DIR}/participant.proto
    ${FFI_PROTO_DIR}/room.proto
    ${FFI_PROTO_DIR}/track.proto
    ${FFI_PROTO_DIR}/video_frame.proto
    ${FFI_PROTO_DIR}/audio_frame.proto
)
set(PROTO_BINARY_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_BINARY_DIR})

find_package(Protobuf REQUIRED) # protobuf::libprotobuf, protoc
find_package(absl CONFIG REQUIRED)

# Object library that owns generated .pb.cc/.pb.h
add_library(livekit_proto OBJECT ${FFI_PROTO_FILES})
target_include_directories(livekit_proto PUBLIC
  "$<BUILD_INTERFACE:${PROTO_BINARY_DIR}>"
  ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(livekit_proto PUBLIC protobuf::libprotobuf)

# Generate .pb sources into ${PROTO_BINARY_DIR} and attach to livekit_proto
protobuf_generate(
  LANGUAGE cpp
  TARGET livekit_proto
  PROTOC_OUT_DIR ${PROTO_BINARY_DIR}
  IMPORT_DIRS ${FFI_PROTO_DIR}
)

# Find cargo
find_program(CARGO_EXECUTABLE NAMES cargo REQUIRED)

set(RUST_ROOT ${CMAKE_SOURCE_DIR}/client-sdk-rust)

# Write a helper script that never passes empty args
set(RUN_CARGO_SCRIPT ${CMAKE_BINARY_DIR}/run_cargo.cmake)
file(WRITE ${RUN_CARGO_SCRIPT}
"if(NOT DEFINED CFG)
  set(CFG Debug)
endif()
if(NOT DEFINED RUST_ROOT)
  message(FATAL_ERROR \"RUST_ROOT not set\")
endif()
if(NOT DEFINED CARGO)
  message(FATAL_ERROR \"CARGO not set\")
endif()

# Build arg list
set(ARGS build)
if(NOT CFG STREQUAL \"Debug\")
  list(APPEND ARGS --release)
endif()

message(STATUS \"[run_cargo.cmake] CFG=\${CFG}  CARGO=\${CARGO}\")
execute_process(
  COMMAND \"\${CARGO}\" \${ARGS}
  WORKING_DIRECTORY \"\${RUST_ROOT}\"
  RESULT_VARIABLE rv
)
if(rv)
  message(FATAL_ERROR \"cargo build failed with code: \${rv}\")
endif()
")

# Imported Rust lib with per-config locations
add_library(livekit_ffi STATIC IMPORTED GLOBAL)
set_target_properties(livekit_ffi PROPERTIES
  IMPORTED_LOCATION_DEBUG           "${RUST_ROOT}/target/debug/liblivekit_ffi.a"
  IMPORTED_LOCATION_RELWITHDEBINFO  "${RUST_ROOT}/target/release/liblivekit_ffi.a"
  IMPORTED_LOCATION_MINSIZEREL      "${RUST_ROOT}/target/release/liblivekit_ffi.a"
  IMPORTED_LOCATION_RELEASE         "${RUST_ROOT}/target/release/liblivekit_ffi.a"
  INTERFACE_INCLUDE_DIRECTORIES     "${RUST_ROOT}/livekit-ffi/include"
)

# Custom target that runs the script; no empty args get passed to cargo
add_custom_target(build_rust_ffi ALL
  COMMAND "${CMAKE_COMMAND}"
          -DCFG=$<CONFIG>
          -DRUST_ROOT=${RUST_ROOT}
          -DCARGO=${CARGO_EXECUTABLE}
          -P "${RUN_CARGO_SCRIPT}"
  USES_TERMINAL
  COMMENT "Invoking cargo for Rust FFI ($<CONFIG>)"
  VERBATIM
)

# ---- C++ wrapper library ----
add_library(livekit
  include/livekit/room.h
  include/livekit/ffi_client.h
  include/livekit/livekit.h
  src/ffi_client.cpp
  src/room.cpp
)
if(APPLE)
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

  target_link_libraries(livekit PUBLIC
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
  )
endif()

# Add generated proto objects to the wrapper
target_sources(livekit PRIVATE $<TARGET_OBJECTS:livekit_proto>)

target_include_directories(livekit PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${RUST_ROOT}/livekit-ffi/include
  ${PROTO_BINARY_DIR}
)

target_link_libraries(livekit
  PUBLIC
    livekit_ffi
    protobuf::libprotobuf
    absl::log
    absl::check
    absl::strings
    absl::base
)


# Ensure cargo runs before we try to link livekit
add_dependencies(livekit build_rust_ffi)

# Warnings
if (MSVC)
  target_compile_options(livekit PRIVATE /permissive- /Zc:__cplusplus /W4)
else()
  target_compile_options(livekit PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Examples ----
add_subdirectory(examples)


# ---------- Clean helpers ----------
# Removes generated protobuf sources
add_custom_target(clean_generated
  COMMAND ${CMAKE_COMMAND} -E echo "Removing generated protobufs: ${PROTO_BINARY_DIR}"
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${PROTO_BINARY_DIR}"
  COMMENT "Clean generated protobuf files"
  VERBATIM
)

# Cargo clean (safer, lets Cargo decide what to delete)
add_custom_target(cargo_clean
  COMMAND ${CMAKE_COMMAND} -E echo "Running 'cargo clean' in: ${RUST_ROOT}"
  COMMAND "${CARGO_EXECUTABLE}" clean
  WORKING_DIRECTORY "${RUST_ROOT}"
  COMMENT "Clean Rust target directory via cargo"
  VERBATIM
)

# Combined "clean-all" (C++ clean + generated + cargo)
# Note: 'clean' is CMake's built-in target that removes CMake-built artifacts.
add_custom_target(clean_all
  # 1) CMake clean (object files, libs, exes)
  COMMAND ${CMAKE_COMMAND} -E echo "==> CMake clean in: ${CMAKE_BINARY_DIR}"
  COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}" --target clean || true
  # 2) Cargo clean (Rust target/)
  COMMAND ${CMAKE_COMMAND} -E echo "==> cargo clean in: ${RUST_ROOT}"
  COMMAND ${CMAKE_COMMAND} -E chdir "${RUST_ROOT}" "${CARGO_EXECUTABLE}" clean || true
  # 3) Remove generated protobufs (lives under build/)
  COMMAND ${CMAKE_COMMAND} -E echo "==> removing generated protobufs: ${PROTO_BINARY_DIR}"
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${PROTO_BINARY_DIR}" || true
  # 4) Remove the entire build directory (like `rm -rf build`)
  #    Switch to SOURCE dir first so removing BINARY dir is always safe.
  COMMAND ${CMAKE_COMMAND} -E echo "==> removing build directory: ${CMAKE_BINARY_DIR}"
  COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_SOURCE_DIR}" ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}" || true
  COMMENT "Full clean: CMake outputs + Rust target + generated protos + delete build/"
  VERBATIM
)