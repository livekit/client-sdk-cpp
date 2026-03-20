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

cmake_minimum_required(VERSION 3.16)
project(realsense_mcap)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Protobuf REQUIRED CONFIG)
find_package(realsense2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
pkg_check_modules(LZ4  REQUIRED IMPORTED_TARGET liblz4)
find_package(ZLIB REQUIRED)

set(REALSENSE_MCAP_DIR ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(realsense_to_mcap
  ${REALSENSE_MCAP_DIR}/src/realsense_to_mcap.cpp
  ${REALSENSE_MCAP_DIR}/BuildFileDescriptorSet.cpp
  ${REALSENSE_MCAP_DIR}/generated/foxglove/RawImage.pb.cc
)

target_include_directories(realsense_to_mcap PRIVATE
  ${realsense2_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/external/mcap/cpp/mcap/include
  ${CMAKE_CURRENT_SOURCE_DIR}/generated
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(realsense_to_mcap
  protobuf::libprotobuf
  ${realsense2_LIBRARY}
  PkgConfig::ZSTD
  PkgConfig::LZ4
)

# LiveKit participants: auto-detect SDK build when this tree lives under client-sdk-cpp (e.g. examples/realsense-livekit/realsense-to-mcap)
set(LiveKitBuild_DIR "" CACHE PATH "Path to LiveKit SDK build directory (optional; auto-detected when under client-sdk-cpp)")
if(NOT LiveKitBuild_DIR)
  get_filename_component(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." REALPATH)
  if(EXISTS "${SDK_ROOT}/build-release/lib")
    set(LiveKitBuild_DIR "${SDK_ROOT}/build-release")
  elseif(EXISTS "${SDK_ROOT}/build/lib")
    set(LiveKitBuild_DIR "${SDK_ROOT}/build")
  endif()
endif()

get_filename_component(LiveKitSource_DIR "${LiveKitBuild_DIR}" DIRECTORY)
set(LIVEKIT_LIB_DIR "${LiveKitBuild_DIR}/lib")
if(NOT LiveKitBuild_DIR OR NOT EXISTS "${LIVEKIT_LIB_DIR}")
  message(FATAL_ERROR "LiveKit SDK build not found. Build the client-sdk-cpp repo from its root (e.g. ./build.sh release), or set -DLiveKitBuild_DIR=<path-to-sdk-build>.")
endif()
find_library(LIVEKIT_BRIDGE_LIB livekit_bridge PATHS "${LIVEKIT_LIB_DIR}" NO_DEFAULT_PATH)
find_library(LIVEKIT_LIB livekit PATHS "${LIVEKIT_LIB_DIR}" NO_DEFAULT_PATH)
if(NOT LIVEKIT_BRIDGE_LIB)
  message(FATAL_ERROR "Could not find liblivekit_bridge in ${LIVEKIT_LIB_DIR}. Build the SDK bridge first.")
endif()
if(NOT LIVEKIT_LIB)
  message(FATAL_ERROR "Could not find liblivekit in ${LIVEKIT_LIB_DIR}.")
endif()
set(LIVEKIT_BRIDGE_INCLUDE "${LiveKitSource_DIR}/bridge/include")
set(LIVEKIT_INCLUDE "${LiveKitSource_DIR}/include")
if(NOT EXISTS "${LIVEKIT_BRIDGE_INCLUDE}/livekit_bridge/livekit_bridge.h")
  message(FATAL_ERROR "LiveKit bridge headers not found at ${LIVEKIT_BRIDGE_INCLUDE}")
endif()

# realsense_rgbd: captures RealSense, publishes RGB as video + depth/pose as DataTracks
add_executable(realsense_rgbd
    ${REALSENSE_MCAP_DIR}/src/realsense_rgbd.cpp
    ${REALSENSE_MCAP_DIR}/BuildFileDescriptorSet.cpp
    ${REALSENSE_MCAP_DIR}/generated/foxglove/RawImage.pb.cc
    ${REALSENSE_MCAP_DIR}/generated/foxglove/PoseInFrame.pb.cc
    ${REALSENSE_MCAP_DIR}/generated/foxglove/Pose.pb.cc
    ${REALSENSE_MCAP_DIR}/generated/foxglove/Quaternion.pb.cc
    ${REALSENSE_MCAP_DIR}/generated/foxglove/Vector3.pb.cc
  )
  target_include_directories(realsense_rgbd PRIVATE
    ${realsense2_INCLUDE_DIR}
    ${LIVEKIT_BRIDGE_INCLUDE}
    ${LIVEKIT_INCLUDE}
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_directories(realsense_rgbd PRIVATE "${LIVEKIT_LIB_DIR}")
  target_link_libraries(realsense_rgbd PRIVATE
    ${LIVEKIT_BRIDGE_LIB}
    ${LIVEKIT_LIB}
    ${realsense2_LIBRARY}
    protobuf::libprotobuf
    ZLIB::ZLIB
  )
  if(UNIX AND NOT APPLE)
    # Embed SDK lib path so the loader finds liblivekit_bridge.so and liblivekit.so at runtime
    set_target_properties(realsense_rgbd PROPERTIES
      BUILD_RPATH "${LIVEKIT_LIB_DIR}"
      INSTALL_RPATH "${LIVEKIT_LIB_DIR}"
      BUILD_WITH_INSTALL_RPATH TRUE
    )
  endif()

  # rgbd_viewer: subscribes to realsense_rgbd video + data track, writes MCAP
  add_executable(rgbd_viewer
    ${REALSENSE_MCAP_DIR}/src/rgbd_viewer.cpp
    ${REALSENSE_MCAP_DIR}/BuildFileDescriptorSet.cpp
    ${REALSENSE_MCAP_DIR}/generated/foxglove/RawImage.pb.cc
  )
  target_include_directories(rgbd_viewer PRIVATE
    ${LIVEKIT_BRIDGE_INCLUDE}
    ${LIVEKIT_INCLUDE}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/mcap/cpp/mcap/include
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_directories(rgbd_viewer PRIVATE "${LIVEKIT_LIB_DIR}")
  target_link_libraries(rgbd_viewer PRIVATE
    ${LIVEKIT_BRIDGE_LIB}
    ${LIVEKIT_LIB}
    protobuf::libprotobuf
    PkgConfig::ZSTD
    PkgConfig::LZ4
    ZLIB::ZLIB
  )
  if(UNIX AND NOT APPLE)
    set_target_properties(rgbd_viewer PROPERTIES
      BUILD_RPATH "${LIVEKIT_LIB_DIR}"
      INSTALL_RPATH "${LIVEKIT_LIB_DIR}"
      BUILD_WITH_INSTALL_RPATH TRUE
    )
  endif()
message(STATUS "LiveKit participants: realsense_rgbd, rgbd_viewer (LiveKitBuild_DIR=${LiveKitBuild_DIR})")
