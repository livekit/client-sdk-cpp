cmake_minimum_required(VERSION 3.10)
project(ros2_livekit_bridge)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(ros2_foxglove_adapters REQUIRED)

# ---------------------------------------------------------------------------
# LiveKit SDK location
#
# Both the main bridge node and the integration test node link against
# livekit_bridge, so the SDK directory is a top-level required setting.
#
# Pass it when configuring:
#   colcon build --packages-select ros2_livekit_bridge \
#     --cmake-args -DLIVEKIT_SDK_DIR=/path/to/build-debug
#
# LIVEKIT_SDK_DIR must point to the CMake build output directory (e.g.
# <repo>/build-debug or <repo>/build-release) that contains lib/ and include/.
# The bridge source headers are resolved automatically from its sibling
# bridge/ directory.
#
# When building the SDK for use with this ROS2 node, you must build the SDK
# *itself* (the client-sdk-cpp project) with -DLIVEKIT_USE_SYSTEM_SPDLOG=ON
# so that livekit_bridge uses system spdlog. Do NOT pass LIVEKIT_USE_SYSTEM_SPDLOG
# to colcon; it has no effect here and the SDK must be rebuilt with that option.
# Otherwise the node can hit SIGBUS in rcl logging (two spdlog/fmt in one process).
# ---------------------------------------------------------------------------
set(LIVEKIT_SDK_DIR "" CACHE PATH
  "Path to the livekit-cpp SDK build directory (e.g. /path/to/build-debug)")

if(NOT LIVEKIT_SDK_DIR OR NOT EXISTS "${LIVEKIT_SDK_DIR}")
  message(FATAL_ERROR
    "LIVEKIT_SDK_DIR must be set to the livekit-cpp build directory, e.g.:\n"
    "  -DLIVEKIT_SDK_DIR=/path/to/client-sdk-cpp/build-debug")
endif()

# The bridge/ source tree lives one level above the build directory.
get_filename_component(_livekit_source_root "${LIVEKIT_SDK_DIR}/.." ABSOLUTE)

find_library(LIVEKIT_FFI_LIB    NAMES livekit_ffi    PATHS "${LIVEKIT_SDK_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(LIVEKIT_LIB        NAMES livekit        PATHS "${LIVEKIT_SDK_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(LIVEKIT_BRIDGE_LIB NAMES livekit_bridge PATHS "${LIVEKIT_SDK_DIR}/lib" NO_DEFAULT_PATH REQUIRED)

message(STATUS "LiveKit SDK dir  : ${LIVEKIT_SDK_DIR}")
message(STATUS "  livekit_ffi    : ${LIVEKIT_FFI_LIB}")
message(STATUS "  livekit        : ${LIVEKIT_LIB}")
message(STATUS "  livekit_bridge : ${LIVEKIT_BRIDGE_LIB}")

# ---------------------------------------------------------------------------
# Main bridge node
# ---------------------------------------------------------------------------
add_executable(ros2_livekit_bridge_node
  src/ros2_livekit_bridge_node.cpp
  src/ros2_livekit_bridge.cpp
)
target_include_directories(ros2_livekit_bridge_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  "${_livekit_source_root}/bridge/include"
  "${LIVEKIT_SDK_DIR}/include"
)
target_compile_features(ros2_livekit_bridge_node PUBLIC cxx_std_17)
target_link_libraries(ros2_livekit_bridge_node
  "${LIVEKIT_BRIDGE_LIB}"
  "${LIVEKIT_LIB}"
  "${LIVEKIT_FFI_LIB}"
  ros2_foxglove_adapters::ros2_foxglove_adapters
)
set_target_properties(ros2_livekit_bridge_node PROPERTIES
  BUILD_RPATH  "${LIVEKIT_SDK_DIR}/lib"
  INSTALL_RPATH "${LIVEKIT_SDK_DIR}/lib"
)
ament_target_dependencies(ros2_livekit_bridge_node
  rclcpp sensor_msgs std_msgs geometry_msgs nav_msgs ros2_foxglove_adapters)

install(TARGETS ros2_livekit_bridge_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config)

install(DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

# ---------------------------------------------------------------------------
# Integration test node
#
# Enable with:
#   colcon build --packages-select ros2_livekit_bridge \
#     --cmake-args -DBUILD_INTEGRATION_TESTS=ON \
#                  -DLIVEKIT_SDK_DIR=/path/to/build-debug
# ---------------------------------------------------------------------------
option(BUILD_INTEGRATION_TESTS
  "Build LiveKit library integration tests" OFF)

if(BUILD_INTEGRATION_TESTS)
  add_executable(livekit_connect_node
    test/integration/livekit_connect_node.cpp
  )
  target_compile_features(livekit_connect_node PUBLIC cxx_std_17)
  target_include_directories(livekit_connect_node PRIVATE
    "${_livekit_source_root}/bridge/include"
    "${LIVEKIT_SDK_DIR}/include"
  )
  target_link_libraries(livekit_connect_node
    "${LIVEKIT_BRIDGE_LIB}"
    "${LIVEKIT_LIB}"
    "${LIVEKIT_FFI_LIB}"
  )
  set_target_properties(livekit_connect_node PROPERTIES
    BUILD_RPATH  "${LIVEKIT_SDK_DIR}/lib"
    INSTALL_RPATH "${LIVEKIT_SDK_DIR}/lib"
  )
  ament_target_dependencies(livekit_connect_node rclcpp)

  install(TARGETS livekit_connect_node
    DESTINATION lib/${PROJECT_NAME})
endif()

ament_package()
