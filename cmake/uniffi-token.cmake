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

set(LIVEKIT_UNIFFI_CPP_BINDGEN_REPOSITORY "https://github.com/alan-george-lk/uniffi-bindgen-cpp.git")
set(LIVEKIT_UNIFFI_CPP_BINDGEN_REVISION "abd001d064d87973f6bb2ee5fd70e30e686a72ae")
set(LIVEKIT_UNIFFI_CPP_BINDGEN_ROOT
    "${RUST_ROOT}/target/uniffi-bindgen-cpp/${LIVEKIT_UNIFFI_CPP_BINDGEN_REVISION}")
set(LIVEKIT_UNIFFI_CPP_BINDGEN
    "${LIVEKIT_UNIFFI_CPP_BINDGEN_ROOT}/bin/uniffi-bindgen-cpp${CMAKE_EXECUTABLE_SUFFIX}")
set(LIVEKIT_UNIFFI_CPP_GENERATED_DIR "${LIVEKIT_BINARY_DIR}/generated/uniffi")

if(WIN32)
  set(LIVEKIT_UNIFFI_LIBRARY_NAME "livekit_uniffi.dll")
  set(LIVEKIT_UNIFFI_IMPLIB_NAME "livekit_uniffi.dll.lib")
elseif(APPLE)
  set(LIVEKIT_UNIFFI_LIBRARY_NAME "liblivekit_uniffi.dylib")
else()
  set(LIVEKIT_UNIFFI_LIBRARY_NAME "liblivekit_uniffi.so")
endif()

set(LIVEKIT_UNIFFI_LIBRARY_DEBUG
    "${RUST_TARGET_DIR}/debug/${LIVEKIT_UNIFFI_LIBRARY_NAME}")
set(LIVEKIT_UNIFFI_LIBRARY_RELEASE
    "${RUST_TARGET_DIR}/release/${LIVEKIT_UNIFFI_LIBRARY_NAME}")

set(LIVEKIT_UNIFFI_CARGO_TARGET_ARGS)
if(RUST_TARGET_TRIPLE)
  list(APPEND LIVEKIT_UNIFFI_CARGO_TARGET_ARGS --target "${RUST_TARGET_TRIPLE}")
endif()

file(GLOB_RECURSE LIVEKIT_UNIFFI_RUST_SOURCES
  "${RUST_ROOT}/livekit-uniffi/src/*.rs"
  "${RUST_ROOT}/livekit-uniffi/Cargo.toml"
  "${RUST_ROOT}/livekit-common/src/*.rs"
  "${RUST_ROOT}/livekit-datatrack/src/*.rs"
  "${RUST_ROOT}/livekit-net/src/*.rs"
)

set(LIVEKIT_UNIFFI_GENERATED_SOURCES
  "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}/livekit_common.cpp"
  "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}/livekit_datatrack.cpp"
  "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}/livekit_net.cpp"
  "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}/livekit_uniffi.cpp"
)
set(LIVEKIT_UNIFFI_GENERATED_HEADERS
  "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}/livekit_common.hpp"
  "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}/livekit_datatrack.hpp"
  "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}/livekit_net.hpp"
  "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}/livekit_uniffi.hpp"
)

add_custom_command(
  OUTPUT ${LIVEKIT_UNIFFI_GENERATED_SOURCES} ${LIVEKIT_UNIFFI_GENERATED_HEADERS}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}"
  COMMAND "${CARGO_EXECUTABLE}" install --locked
          --git "${LIVEKIT_UNIFFI_CPP_BINDGEN_REPOSITORY}"
          --rev "${LIVEKIT_UNIFFI_CPP_BINDGEN_REVISION}"
          --root "${LIVEKIT_UNIFFI_CPP_BINDGEN_ROOT}"
          uniffi-bindgen-cpp
  COMMAND "${CMAKE_COMMAND}" -E env "PROTOC=${Protobuf_PROTOC_EXECUTABLE}"
          "${CARGO_EXECUTABLE}" build --package livekit-uniffi
          $<$<NOT:$<CONFIG:Debug>>:--release>
          ${LIVEKIT_UNIFFI_CARGO_TARGET_ARGS}
  COMMAND "${LIVEKIT_UNIFFI_CPP_BINDGEN}"
          --library
          "$<IF:$<CONFIG:Debug>,${LIVEKIT_UNIFFI_LIBRARY_DEBUG},${LIVEKIT_UNIFFI_LIBRARY_RELEASE}>"
          --out-dir "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}"
  WORKING_DIRECTORY "${RUST_ROOT}"
  DEPENDS ${LIVEKIT_UNIFFI_RUST_SOURCES}
  COMMENT "Generating C++ bindings for the UniFFI access-token smoke test"
  COMMAND_EXPAND_LISTS
  VERBATIM
)

set_source_files_properties(
  ${LIVEKIT_UNIFFI_GENERATED_SOURCES}
  ${LIVEKIT_UNIFFI_GENERATED_HEADERS}
  PROPERTIES GENERATED TRUE
)

add_library(livekit_uniffi_cpp STATIC
  ${LIVEKIT_UNIFFI_GENERATED_SOURCES}
  ${LIVEKIT_UNIFFI_GENERATED_HEADERS}
)
target_compile_features(livekit_uniffi_cpp PRIVATE cxx_std_17)
target_include_directories(livekit_uniffi_cpp PUBLIC "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}")
livekit_disable_warnings(livekit_uniffi_cpp)

add_library(livekit_uniffi SHARED IMPORTED GLOBAL)
set_target_properties(livekit_uniffi PROPERTIES
  IMPORTED_LOCATION_DEBUG "${LIVEKIT_UNIFFI_LIBRARY_DEBUG}"
  IMPORTED_LOCATION_RELEASE "${LIVEKIT_UNIFFI_LIBRARY_RELEASE}"
  IMPORTED_LOCATION_RELWITHDEBINFO "${LIVEKIT_UNIFFI_LIBRARY_RELEASE}"
  IMPORTED_LOCATION_MINSIZEREL "${LIVEKIT_UNIFFI_LIBRARY_RELEASE}"
)
if(WIN32)
  set_target_properties(livekit_uniffi PROPERTIES
    IMPORTED_IMPLIB_DEBUG "${RUST_TARGET_DIR}/debug/${LIVEKIT_UNIFFI_IMPLIB_NAME}"
    IMPORTED_IMPLIB_RELEASE "${RUST_TARGET_DIR}/release/${LIVEKIT_UNIFFI_IMPLIB_NAME}"
    IMPORTED_IMPLIB_RELWITHDEBINFO "${RUST_TARGET_DIR}/release/${LIVEKIT_UNIFFI_IMPLIB_NAME}"
    IMPORTED_IMPLIB_MINSIZEREL "${RUST_TARGET_DIR}/release/${LIVEKIT_UNIFFI_IMPLIB_NAME}"
  )
endif()
target_link_libraries(livekit_uniffi_cpp PUBLIC livekit_uniffi)

find_package(Threads REQUIRED)
target_link_libraries(livekit_uniffi_cpp PRIVATE Threads::Threads)
