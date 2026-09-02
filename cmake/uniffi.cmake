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

# Keep this pin aligned with client-sdk-rust/livekit-uniffi/Makefile.toml.
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

set(LIVEKIT_UNIFFI_LIBRARY_DEBUG "${RUST_TARGET_DIR}/debug/${LIVEKIT_UNIFFI_LIBRARY_NAME}")
set(LIVEKIT_UNIFFI_LIBRARY_RELEASE "${RUST_TARGET_DIR}/release/${LIVEKIT_UNIFFI_LIBRARY_NAME}")

file(GLOB_RECURSE LIVEKIT_UNIFFI_RUST_SOURCES
  "${RUST_ROOT}/livekit-uniffi/src/*.rs"
  "${RUST_ROOT}/livekit-uniffi/Cargo.toml"
  "${RUST_ROOT}/livekit-common/src/*.rs"
  "${RUST_ROOT}/livekit-common/Cargo.toml"
  "${RUST_ROOT}/livekit-datatrack/src/*.rs"
  "${RUST_ROOT}/livekit-datatrack/Cargo.toml"
  "${RUST_ROOT}/livekit-net/src/*.rs"
  "${RUST_ROOT}/livekit-net/Cargo.toml"
  "${RUST_ROOT}/livekit-token/src/*.rs"
  "${RUST_ROOT}/livekit-token/Cargo.toml"
  "${RUST_ROOT}/livekit-protocol/src/*.rs"
  "${RUST_ROOT}/livekit-protocol/Cargo.toml"
  "${RUST_ROOT}/Cargo.toml"
  "${RUST_ROOT}/Cargo.lock"
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
  OUTPUT "${LIVEKIT_UNIFFI_CPP_BINDGEN}"
  COMMAND "${CARGO_EXECUTABLE}" install --locked
          --git "${LIVEKIT_UNIFFI_CPP_BINDGEN_REPOSITORY}"
          --rev "${LIVEKIT_UNIFFI_CPP_BINDGEN_REVISION}"
          --root "${LIVEKIT_UNIFFI_CPP_BINDGEN_ROOT}"
          uniffi-bindgen-cpp
  COMMENT "Installing pinned uniffi-bindgen-cpp"
  VERBATIM
)
add_custom_target(build_uniffi_bindgen DEPENDS "${LIVEKIT_UNIFFI_CPP_BINDGEN}")

add_custom_command(
  OUTPUT "${LIVEKIT_UNIFFI_LIBRARY_DEBUG}" "${LIVEKIT_UNIFFI_LIBRARY_RELEASE}"
  COMMAND "${CMAKE_COMMAND}"
          -DCFG=$<CONFIG>
          -DRUST_ROOT=${RUST_ROOT}
          -DCARGO=${CARGO_EXECUTABLE}
          -DPROTOC_PATH=${Protobuf_PROTOC_EXECUTABLE}
          -DRUST_TARGET=${RUST_TARGET_TRIPLE}
          -DGCC_LIB_DIR=${GCC_LIB_DIR}
          -DPACKAGE=livekit-uniffi
          -P "${RUN_CARGO_SCRIPT}"
  WORKING_DIRECTORY "${RUST_ROOT}"
  DEPENDS ${LIVEKIT_UNIFFI_RUST_SOURCES}
  COMMENT "Building livekit-uniffi via cargo"
  VERBATIM
)
add_custom_target(build_rust_uniffi
  DEPENDS "${LIVEKIT_UNIFFI_LIBRARY_DEBUG}" "${LIVEKIT_UNIFFI_LIBRARY_RELEASE}"
)

add_custom_command(
  OUTPUT ${LIVEKIT_UNIFFI_GENERATED_SOURCES} ${LIVEKIT_UNIFFI_GENERATED_HEADERS}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}"
  COMMAND "${LIVEKIT_UNIFFI_CPP_BINDGEN}"
          --library
          "$<IF:$<CONFIG:Debug>,${LIVEKIT_UNIFFI_LIBRARY_DEBUG},${LIVEKIT_UNIFFI_LIBRARY_RELEASE}>"
          --out-dir "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}"
  WORKING_DIRECTORY "${RUST_ROOT}"
  DEPENDS build_uniffi_bindgen build_rust_uniffi ${LIVEKIT_UNIFFI_RUST_SOURCES}
  COMMENT "Generating C++ bindings for livekit-uniffi"
  VERBATIM
)

set_source_files_properties(
  ${LIVEKIT_UNIFFI_GENERATED_SOURCES} ${LIVEKIT_UNIFFI_GENERATED_HEADERS}
  PROPERTIES GENERATED TRUE
)

add_library(livekit_uniffi SHARED IMPORTED GLOBAL)
set_target_properties(livekit_uniffi PROPERTIES
  IMPORTED_CONFIGURATIONS "Debug;Release;RelWithDebInfo;MinSizeRel"
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
elseif(UNIX AND NOT APPLE)
  set_target_properties(livekit_uniffi PROPERTIES IMPORTED_NO_SONAME TRUE)
endif()

add_library(livekit_uniffi_cpp STATIC
  ${LIVEKIT_UNIFFI_GENERATED_SOURCES} ${LIVEKIT_UNIFFI_GENERATED_HEADERS}
)
add_dependencies(livekit_uniffi_cpp build_rust_uniffi)
set_target_properties(livekit_uniffi_cpp PROPERTIES EXPORT_NAME uniffi)
target_compile_features(livekit_uniffi_cpp PRIVATE cxx_std_17)
target_include_directories(livekit_uniffi_cpp
  PUBLIC
    $<BUILD_INTERFACE:${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/livekit/uniffi>
)
target_link_libraries(livekit_uniffi_cpp PRIVATE $<BUILD_INTERFACE:livekit_uniffi>)
livekit_disable_warnings(livekit_uniffi_cpp)

if(LIVEKIT_IS_TOPLEVEL)
  add_custom_command(TARGET livekit_uniffi_cpp POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            $<TARGET_FILE:livekit_uniffi>
            "$<TARGET_FILE_DIR:livekit_uniffi_cpp>/${LIVEKIT_UNIFFI_LIBRARY_NAME}"
    COMMENT "Copying livekit-uniffi runtime to output directory"
  )
  if(APPLE)
    add_custom_command(TARGET livekit_uniffi_cpp POST_BUILD
      COMMAND /usr/bin/install_name_tool
              -id "@rpath/liblivekit_uniffi.dylib"
              "$<TARGET_FILE_DIR:livekit_uniffi_cpp>/liblivekit_uniffi.dylib"
      COMMENT "Fix install_name id for liblivekit_uniffi.dylib"
      VERBATIM
    )
  endif()
endif()

install(TARGETS livekit_uniffi_cpp
  EXPORT LiveKitTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/livekit/uniffi
)
install(DIRECTORY "${LIVEKIT_UNIFFI_CPP_GENERATED_DIR}/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/livekit/uniffi"
  FILES_MATCHING PATTERN "*.hpp"
)
install(IMPORTED_RUNTIME_ARTIFACTS livekit_uniffi
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
if(WIN32)
  install(FILES
    $<TARGET_PROPERTY:livekit_uniffi,IMPORTED_IMPLIB_RELEASE>
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    CONFIGURATIONS Release RelWithDebInfo MinSizeRel
  )
  install(FILES
    $<TARGET_PROPERTY:livekit_uniffi,IMPORTED_IMPLIB_DEBUG>
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    CONFIGURATIONS Debug
  )
endif()
