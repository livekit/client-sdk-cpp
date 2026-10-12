# Copyright 2026 LiveKit
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.20)
project(livekit_packaging_tests LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# This is a standalone consumer of an installed SDK, not the in-tree target.
find_package(LiveKit CONFIG REQUIRED)
set(LIVEKIT_EXPECT_CAPTURE "ON" CACHE STRING "Expected capture support in the installed SDK (ON or OFF)")
set_property(CACHE LIVEKIT_EXPECT_CAPTURE PROPERTY STRINGS ON OFF)
if(NOT LIVEKIT_EXPECT_CAPTURE STREQUAL "ON" AND NOT LIVEKIT_EXPECT_CAPTURE STREQUAL "OFF")
  message(FATAL_ERROR "LIVEKIT_EXPECT_CAPTURE must be ON or OFF")
endif()
set(LIVEKIT_EXPECT_VERSION "" CACHE STRING "Expected release version; empty skips release-specific checks")

# Check the installed snapshot, not whichever variant the SDK build tree last held.
get_target_property(_sdk_include_dirs LiveKit::livekit INTERFACE_INCLUDE_DIRECTORIES)
list(GET _sdk_include_dirs 0 _sdk_include_dir)
get_filename_component(_sdk_root "${_sdk_include_dir}/.." ABSOLUTE)
foreach(_file IN ITEMS
    "${_sdk_root}/include/livekit/livekit.h"
    "${_sdk_root}/include/livekit/build.h"
    "${_sdk_root}/share/livekit/build-info.json"
    "${LiveKit_DIR}/LiveKitConfig.cmake"
    "${LiveKit_DIR}/LiveKitConfigVersion.cmake"
    "${LiveKit_DIR}/LiveKitTargets.cmake")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "Installed SDK is missing ${_file}")
  endif()
endforeach()
file(READ "${_sdk_root}/share/livekit/build-info.json" _build_info)
string(JSON _capture GET "${_build_info}" capture_enabled)
if(LIVEKIT_EXPECT_CAPTURE STREQUAL "ON")
  set(_expected_capture 1)
else()
  set(_expected_capture 0)
endif()
if(NOT _capture STREQUAL "${_expected_capture}")
  message(FATAL_ERROR "Installed build-info.json capture setting does not match ${LIVEKIT_EXPECT_CAPTURE}")
endif()

if(NOT LIVEKIT_EXPECT_VERSION STREQUAL "")
  file(READ "${_sdk_root}/include/livekit/build.h" _build_header)
  string(FIND "${_build_header}" "#define LIVEKIT_BUILD_VERSION \"${LIVEKIT_EXPECT_VERSION}\"" _version_position)
  string(JSON _version GET "${_build_info}" sdk_version)
  string(JSON _rust_commit GET "${_build_info}" rust_submodule_commit)
  string(JSON _ffi_version GET "${_build_info}" livekit_ffi_version)
  string(LENGTH "${_rust_commit}" _commit_length)
  if(_version_position EQUAL -1 OR NOT _version STREQUAL LIVEKIT_EXPECT_VERSION OR _version STREQUAL "0.0.0")
    message(FATAL_ERROR "Installed SDK version does not match ${LIVEKIT_EXPECT_VERSION}")
  endif()
  if(NOT _rust_commit MATCHES "^[0-9a-f]+$" OR _commit_length LESS 7)
    message(FATAL_ERROR "Installed SDK is missing its Rust submodule commit")
  endif()
  if(NOT _ffi_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+")
    message(FATAL_ERROR "Installed SDK is missing its livekit-ffi version")
  endif()
endif()

add_executable(livekit_capture_package_test test_capture_bundle.cpp)
target_compile_features(livekit_capture_package_test PRIVATE cxx_std_17)
target_link_libraries(livekit_capture_package_test PRIVATE LiveKit::livekit)
# The installed macOS SDK uses @rpath for its FFI dependency.
if(APPLE)
  set_property(TARGET livekit_capture_package_test PROPERTY BUILD_RPATH "$<TARGET_FILE_DIR:LiveKit::livekit>")
endif()

enable_testing()
add_test(NAME InstalledSdk.Capture
  COMMAND livekit_capture_package_test "--expect-capture=${LIVEKIT_EXPECT_CAPTURE}")
set_tests_properties(InstalledSdk.Capture PROPERTIES
  LABELS "packaging;integration"
  TIMEOUT 30
)
