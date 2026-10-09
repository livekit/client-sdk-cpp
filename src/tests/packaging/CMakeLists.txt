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
