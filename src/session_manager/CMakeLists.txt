cmake_minimum_required(VERSION 3.20)

project(session_manager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(session_manager SHARED
  src/session_manager.cpp
  src/managed_audio_track.cpp
  src/managed_video_track.cpp
  src/bridge_room_delegate.cpp
  src/bridge_room_delegate.h
  src/rpc_controller.cpp
  src/rpc_controller.h
)

if(WIN32)
  set_target_properties(session_manager PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

target_include_directories(session_manager
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LIVEKIT_ROOT_DIR}/src
)

# Link against the main livekit SDK library (which transitively provides
# include paths for livekit/*.h and links livekit_ffi).
target_link_libraries(session_manager
  PUBLIC
    livekit
  PRIVATE
    spdlog::spdlog
)

target_compile_definitions(session_manager
  PRIVATE
    SPDLOG_ACTIVE_LEVEL=${_SPDLOG_ACTIVE_LEVEL}
)

if(MSVC)
  target_compile_options(session_manager PRIVATE /permissive- /Zc:__cplusplus /W4)
else()
  target_compile_options(session_manager PRIVATE -Wall -Wextra -Wpedantic)
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
