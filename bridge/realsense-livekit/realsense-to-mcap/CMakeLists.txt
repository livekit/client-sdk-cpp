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

find_package(Protobuf REQUIRED)
find_package(realsense2 REQUIRED)

set(REALSENSE_MCAP_DIR ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(realsense_mcap
  ${REALSENSE_MCAP_DIR}/main.cpp
  ${REALSENSE_MCAP_DIR}/BuildFileDescriptorSet.cpp
  ${REALSENSE_MCAP_DIR}/generated/foxglove/RawImage.pb.cc
)

target_include_directories(realsense_mcap PRIVATE
  ${Protobuf_INCLUDE_DIRS}
  ${realsense2_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/mcap/cpp/mcap/include
  ${CMAKE_CURRENT_SOURCE_DIR}/generated
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(realsense_mcap
  ${Protobuf_LIBRARIES}
  ${realsense2_LIBRARY}
  zstd
  lz4
)

# ---- LiveKit participants (optional; use README build, then enable with -DBUILD_LIVEKIT_PARTICIPANTS=ON -DLiveKitBuild_DIR=<sdk-build>) ----
option(BUILD_LIVEKIT_PARTICIPANTS "Build realsense_rgbd and rgbd_viewer (requires LiveKit SDK build)" OFF)

if(BUILD_LIVEKIT_PARTICIPANTS)
  set(LiveKitBuild_DIR "" CACHE PATH "Path to LiveKit SDK build directory (e.g. <repo>/build-release) containing lib/")
  get_filename_component(LiveKitSource_DIR "${LiveKitBuild_DIR}" DIRECTORY)
  set(LIVEKIT_LIB_DIR "${LiveKitBuild_DIR}/lib")
  if(NOT LiveKitBuild_DIR OR NOT EXISTS "${LIVEKIT_LIB_DIR}")
    message(FATAL_ERROR "BUILD_LIVEKIT_PARTICIPANTS=ON requires -DLiveKitBuild_DIR=<path-to-sdk-build> (e.g. <repo>/build-release)")
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

  # realsense_rgbd: captures RealSense, publishes RGB as video + depth as DataTrack
  add_executable(realsense_rgbd
    ${REALSENSE_MCAP_DIR}/src/realsense_rgbd.cpp
    ${REALSENSE_MCAP_DIR}/BuildFileDescriptorSet.cpp
    ${REALSENSE_MCAP_DIR}/generated/foxglove/RawImage.pb.cc
  )
  target_include_directories(realsense_rgbd PRIVATE
    ${Protobuf_INCLUDE_DIRS}
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
    ${Protobuf_LIBRARIES}
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
    ${Protobuf_INCLUDE_DIRS}
    ${LIVEKIT_BRIDGE_INCLUDE}
    ${LIVEKIT_INCLUDE}
    ${CMAKE_CURRENT_SOURCE_DIR}/mcap/cpp/mcap/include
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_directories(rgbd_viewer PRIVATE "${LIVEKIT_LIB_DIR}")
  target_link_libraries(rgbd_viewer PRIVATE
    ${LIVEKIT_BRIDGE_LIB}
    ${LIVEKIT_LIB}
    ${Protobuf_LIBRARIES}
    zstd
    lz4
  )
  if(UNIX AND NOT APPLE)
    set_target_properties(rgbd_viewer PROPERTIES
      BUILD_RPATH "${LIVEKIT_LIB_DIR}"
      INSTALL_RPATH "${LIVEKIT_LIB_DIR}"
      BUILD_WITH_INSTALL_RPATH TRUE
    )
  endif()
  message(STATUS "LiveKit participants: realsense_rgbd, rgbd_viewer (LiveKitBuild_DIR=${LiveKitBuild_DIR})")
endif()
