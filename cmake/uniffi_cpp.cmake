# Copyright 2026 LiveKit, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(LIVEKIT_UNIFFI_CPP_GENERATED_DIR "${LIVEKIT_BINARY_DIR}/generated/uniffi")
set(LIVEKIT_UNIFFI_CPP_SOURCE
    "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}/livekit_ffi.cpp")
set(LIVEKIT_UNIFFI_CPP_HEADER
    "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}/livekit_ffi.hpp")
set(LIVEKIT_UNIFFI_CPP_SCAFFOLDING_HEADER
    "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}/livekit_ffi_scaffolding.hpp")

# Generate from an unstripped host library. Release Linux builds strip the
# UniFFI metadata symbols, while generated C++ source is platform-independent.
set(LIVEKIT_UNIFFI_METADATA_TARGET_DIR
    "${RUST_ROOT}/target/uniffi-cpp-metadata")
if(WIN32)
  set(LIVEKIT_UNIFFI_METADATA_LIBRARY
      "${LIVEKIT_UNIFFI_METADATA_TARGET_DIR}/debug/livekit_ffi.dll")
elseif(APPLE)
  set(LIVEKIT_UNIFFI_METADATA_LIBRARY
      "${LIVEKIT_UNIFFI_METADATA_TARGET_DIR}/debug/liblivekit_ffi.dylib")
else()
  set(LIVEKIT_UNIFFI_METADATA_LIBRARY
      "${LIVEKIT_UNIFFI_METADATA_TARGET_DIR}/debug/liblivekit_ffi.so")
endif()

file(GLOB_RECURSE LIVEKIT_UNIFFI_RUST_SOURCES CONFIGURE_DEPENDS
  "${RUST_ROOT}/livekit-ffi/src/*.rs"
  "${RUST_ROOT}/livekit-ffi/Cargo.toml"
)
list(APPEND LIVEKIT_UNIFFI_RUST_SOURCES
  "${RUST_ROOT}/Cargo.toml"
  "${RUST_ROOT}/Cargo.lock"
  "${RUST_ROOT}/rust-toolchain.toml"
)

file(GLOB_RECURSE LIVEKIT_UNIFFI_BINDGEN_SOURCES CONFIGURE_DEPENDS
  "${RUST_ROOT}/tools/bindgens/src/*.rs"
  "${RUST_ROOT}/tools/bindgens/Cargo.toml"
)

add_custom_command(
  OUTPUT "${LIVEKIT_UNIFFI_METADATA_LIBRARY}"
  COMMAND "${CMAKE_COMMAND}"
          -DCFG=Debug
          -DRUST_ROOT=${RUST_ROOT}
          -DCARGO=${CARGO_EXECUTABLE}
          -DPROTOC_PATH=${Protobuf_PROTOC_EXECUTABLE}
          -DGCC_LIB_DIR=${GCC_LIB_DIR}
          -DCARGO_TARGET_DIR=${LIVEKIT_UNIFFI_METADATA_TARGET_DIR}
          -P "${RUN_CARGO_SCRIPT}"
  WORKING_DIRECTORY "${RUST_ROOT}"
  DEPENDS ${LIVEKIT_UNIFFI_RUST_SOURCES}
  COMMENT "Building unstripped livekit-ffi metadata library"
  VERBATIM
)
add_custom_target(build_livekit_ffi_metadata
  DEPENDS "${LIVEKIT_UNIFFI_METADATA_LIBRARY}")
# Both commands invoke rustup/Cargo and share toolchain state. Keep them
# serialized so fresh CI runners cannot race while installing the toolchain.
add_dependencies(build_livekit_ffi_metadata build_rust_ffi)

add_custom_command(
  OUTPUT
    "${LIVEKIT_UNIFFI_CPP_SOURCE}"
    "${LIVEKIT_UNIFFI_CPP_HEADER}"
    "${LIVEKIT_UNIFFI_CPP_SCAFFOLDING_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
          "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          "CARGO_TARGET_DIR=${RUST_ROOT}/target/bindgens"
          "${CARGO_EXECUTABLE}" run --locked
          --package bindgens
          --bin uniffi-bindgen-cpp
          --
          --library "${LIVEKIT_UNIFFI_METADATA_LIBRARY}"
          --out-dir "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}"
  WORKING_DIRECTORY "${RUST_ROOT}"
  DEPENDS
    build_livekit_ffi_metadata
    ${LIVEKIT_UNIFFI_RUST_SOURCES}
    ${LIVEKIT_UNIFFI_BINDGEN_SOURCES}
  COMMENT "Generating UniFFI C++ bindings"
  VERBATIM
)
add_custom_target(generate_livekit_ffi_uniffi_cpp
  DEPENDS
    "${LIVEKIT_UNIFFI_CPP_SOURCE}"
    "${LIVEKIT_UNIFFI_CPP_HEADER}"
    "${LIVEKIT_UNIFFI_CPP_SCAFFOLDING_HEADER}"
)

set_source_files_properties(
  "${LIVEKIT_UNIFFI_CPP_SOURCE}"
  "${LIVEKIT_UNIFFI_CPP_HEADER}"
  "${LIVEKIT_UNIFFI_CPP_SCAFFOLDING_HEADER}"
  PROPERTIES GENERATED TRUE
)

add_library(livekit_uniffi_cpp OBJECT
  "${LIVEKIT_UNIFFI_CPP_SOURCE}"
)
add_dependencies(livekit_uniffi_cpp generate_livekit_ffi_uniffi_cpp)
target_include_directories(livekit_uniffi_cpp
  PUBLIC "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}"
)
target_link_libraries(livekit_uniffi_cpp PRIVATE livekit_ffi)
