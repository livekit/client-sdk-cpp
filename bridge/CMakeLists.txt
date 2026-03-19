cmake_minimum_required(VERSION 3.20)

project(livekit_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(livekit_bridge SHARED
  src/livekit_bridge.cpp
  src/bridge_audio_track.cpp
  src/bridge_video_track.cpp
  src/bridge_room_delegate.cpp
  src/bridge_room_delegate.h
  src/rpc_controller.cpp
  src/rpc_controller.h
)

if(WIN32)
  set_target_properties(livekit_bridge PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

target_include_directories(livekit_bridge
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LIVEKIT_ROOT_DIR}/src
)

# Link against the main livekit SDK library (which transitively provides
# include paths for livekit/*.h and links livekit_ffi).
target_link_libraries(livekit_bridge
  PUBLIC
    livekit
  PRIVATE
    spdlog::spdlog
)

target_compile_definitions(livekit_bridge
  PRIVATE
    SPDLOG_ACTIVE_LEVEL=${_SPDLOG_ACTIVE_LEVEL}
)

if(MSVC)
  target_compile_options(livekit_bridge PRIVATE /permissive- /Zc:__cplusplus /W4)
else()
  target_compile_options(livekit_bridge PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Tests ---
# Bridge tests default to OFF. They are automatically enabled when the parent
# SDK tests are enabled (LIVEKIT_BUILD_TESTS=ON), e.g. via ./build.sh debug-tests.
option(LIVEKIT_BRIDGE_BUILD_TESTS "Build bridge unit tests" OFF)

if(LIVEKIT_BRIDGE_BUILD_TESTS OR LIVEKIT_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# Bridge examples (robot + human) are built from examples/CMakeLists.txt
# when LIVEKIT_BUILD_EXAMPLES is ON; see examples/bridge_human_robot/.
