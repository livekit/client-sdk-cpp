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

cmake_minimum_required(VERSION 3.25)
project(livekit_packaging_tests LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# This is a standalone consumer of an installed SDK, not the in-tree target.
find_package(LiveKit CONFIG REQUIRED)
set(LIVEKIT_CAPTURE "all" CACHE STRING
  "Expected capture sources: none, all, or comma-separated gstreamer,pattern,clock")
set_property(CACHE LIVEKIT_CAPTURE PROPERTY STRINGS
  "" none all gstreamer pattern clock)
option(LIVEKIT_VERIFY_PIXEL_SOURCES "Require successful GPU pattern and clock source creation" OFF)
string(STRIP "${LIVEKIT_CAPTURE}" _source_selection)
string(TOLOWER "${_source_selection}" _source_selection)
set(_expected_sources "")
if(_source_selection STREQUAL "all")
  set(_expected_sources clock gstreamer pattern)
elseif(NOT _source_selection STREQUAL "" AND NOT _source_selection STREQUAL "none")
  string(REPLACE "," ";" _expected_sources "${_source_selection}")
endif()
set(_expected_features "")
foreach(_source IN LISTS _expected_sources)
  string(STRIP "${_source}" _source)
  if(NOT _source MATCHES "^(clock|gstreamer|pattern)$")
    message(FATAL_ERROR
      "Unknown LIVEKIT_CAPTURE source '${_source}'. "
      "Expected: gstreamer, pattern, clock, all, or none.")
  endif()
  list(APPEND _expected_features "capture-${_source}")
endforeach()
list(REMOVE_DUPLICATES _expected_sources)
list(REMOVE_DUPLICATES _expected_features)
list(SORT _expected_features)
list(JOIN _expected_features "," _expected_features_csv)
if(_expected_sources)
  set(_expected_capture 1)
else()
  set(_expected_capture 0)
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
string(JSON _features GET "${_build_info}" cargo_features)
string(REPLACE "," ";" _installed_features "${_features}")
list(REMOVE_DUPLICATES _installed_features)
list(SORT _installed_features)
list(JOIN _installed_features "," _installed_features_csv)
if(NOT _installed_features_csv STREQUAL _expected_features_csv)
  message(FATAL_ERROR "Installed cargo_features '${_features}' do not match expected '${_expected_features_csv}'")
endif()
if(NOT _capture STREQUAL "${_expected_capture}")
  message(FATAL_ERROR "Installed build-info.json capture setting does not match LIVEKIT_CAPTURE='${LIVEKIT_CAPTURE}'")
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

# Validate the public header layout as well as compile-time availability.
if(EXISTS "${_sdk_include_dir}/livekit/capture_source.h")
  message(FATAL_ERROR "Installed SDK contains the obsolete flat capture header")
endif()
if(_expected_capture)
  if(NOT EXISTS "${_sdk_include_dir}/livekit/capture/capture_source.h")
    message(FATAL_ERROR "Capture SDK is missing the capture public API directory")
  endif()
elseif(EXISTS "${_sdk_include_dir}/livekit/capture")
  message(FATAL_ERROR "Standard SDK must not install the capture public API directory")
endif()

# Direct capture includes must fail in standard bundles and work in capture bundles.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES "${_sdk_include_dir}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
unset(_capture_header_compiles CACHE)
check_cxx_source_compiles("#include <livekit/capture/capture_source.h>
int main() { return 0; }"
  _capture_header_compiles)
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
if(_expected_capture AND NOT _capture_header_compiles)
  message(FATAL_ERROR "Capture SDK must allow including livekit/capture/capture_source.h")
elseif(NOT _expected_capture AND _capture_header_compiles)
  message(FATAL_ERROR "Standard SDK must reject including livekit/capture/capture_source.h")
endif()

add_executable(livekit_capture_package_test test_capture_bundle.cpp)
target_compile_features(livekit_capture_package_test PRIVATE cxx_std_17)
foreach(_source IN ITEMS clock gstreamer pattern)
  string(TOUPPER "${_source}" _source_upper)
  if(_source IN_LIST _expected_sources)
    set(_enabled 1)
  else()
    set(_enabled 0)
  endif()
  target_compile_definitions(livekit_capture_package_test PRIVATE "LIVEKIT_EXPECT_${_source_upper}=${_enabled}")
endforeach()
target_compile_definitions(livekit_capture_package_test PRIVATE
  LIVEKIT_VERIFY_PIXEL_SOURCES=$<BOOL:${LIVEKIT_VERIFY_PIXEL_SOURCES}>)
target_link_libraries(livekit_capture_package_test PRIVATE LiveKit::livekit)
# The installed macOS SDK uses @rpath for its FFI dependency.
if(APPLE)
  set_property(TARGET livekit_capture_package_test PROPERTY BUILD_RPATH "$<TARGET_FILE_DIR:LiveKit::livekit>")
endif()

enable_testing()
add_test(NAME InstalledSdk.Capture COMMAND livekit_capture_package_test)
set_tests_properties(InstalledSdk.Capture PROPERTIES
  LABELS "packaging;integration"
  TIMEOUT 60
)
