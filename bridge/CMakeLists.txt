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
)

# Link against the main livekit SDK library (which transitively provides
# include paths for livekit/*.h and links livekit_ffi).
target_link_libraries(livekit_bridge
  PUBLIC
    livekit
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

# --- Examples ---
option(LIVEKIT_BRIDGE_BUILD_EXAMPLES "Build the bridge examples" ON)

if(LIVEKIT_BRIDGE_BUILD_EXAMPLES)
  # ---- Set RPATH so examples find shared libs in the executable directory ----
  if(UNIX)
    if(APPLE)
      set(CMAKE_BUILD_RPATH "@loader_path;@loader_path/../lib")
      set(CMAKE_INSTALL_RPATH "@loader_path;@loader_path/../lib")
    else()
      set(CMAKE_BUILD_RPATH "$ORIGIN:$ORIGIN/../lib")
      set(CMAKE_INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib")
    endif()
    set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
    set(CMAKE_SKIP_BUILD_RPATH FALSE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
  endif()

  # ---- SDL3 (shared by robot and human targets) ----
  list(APPEND CMAKE_MODULE_PATH "${LIVEKIT_ROOT_DIR}/examples/cmake")
  include(sdl3)

  # Path to the SDL media helpers in examples/simple_room/
  set(SDL_MEDIA_DIR "${LIVEKIT_ROOT_DIR}/examples/simple_room")

  # ---- Stub examples (no SDL, synthetic data) ----
  add_executable(robot_stub examples/robot_stub.cpp)
  target_link_libraries(robot_stub PRIVATE livekit_bridge)

  add_executable(human_stub examples/human_stub.cpp)
  target_link_libraries(human_stub PRIVATE livekit_bridge)

  # ---- Robot: Real webcam + mic via SDL3 ----
  add_executable(robot
    examples/robot.cpp
    ${SDL_MEDIA_DIR}/sdl_media.cpp
    ${SDL_MEDIA_DIR}/sdl_media.h
  )
  target_include_directories(robot PRIVATE ${SDL_MEDIA_DIR})
  target_link_libraries(robot PRIVATE livekit_bridge SDL3::SDL3)

  # ---- Human: SDL3 video display + speaker playback ----
  add_executable(human
    examples/human.cpp
    ${SDL_MEDIA_DIR}/sdl_media.cpp
    ${SDL_MEDIA_DIR}/sdl_media.h
  )
  target_include_directories(human PRIVATE ${SDL_MEDIA_DIR})
  target_link_libraries(human PRIVATE livekit_bridge SDL3::SDL3)

  # ---- Copy SDL3 shared library to robot/human output directories, avoids duplicate code ----
  set(_SDL_TARGETS robot human)
  foreach(_target ${_SDL_TARGETS})
    if(UNIX AND NOT APPLE)
      add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>"
                "$<TARGET_FILE_DIR:${_target}>"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                "$<TARGET_FILE_NAME:SDL3::SDL3>"
                "$<TARGET_FILE_DIR:${_target}>/$<TARGET_SONAME_FILE_NAME:SDL3::SDL3>"
        COMMENT "Copying SDL3 shared library and SONAME symlink to ${_target} output directory"
        VERBATIM
      )
    else()
      add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>"
                "$<TARGET_FILE_DIR:${_target}>"
        COMMENT "Copying SDL3 shared library to ${_target} output directory"
        VERBATIM
      )
    endif()
  endforeach()
endif()
