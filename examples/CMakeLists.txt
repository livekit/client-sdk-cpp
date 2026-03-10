cmake_minimum_required(VERSION 3.20)
project(livekit-examples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set RPATH for examples to find shared libraries in executable directory
# This ensures SDL3 and other shared libs are loaded from bin/ not _deps/
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

# Always use FetchContent for SDL3 (vcpkg doesn't have it in stable baseline)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(sdl3)

# Common include directories for examples that need private headers
# TODO: These should be refactored to use only public headers
set(EXAMPLES_PRIVATE_INCLUDE_DIRS
    ${LIVEKIT_ROOT_DIR}/src
    ${LIVEKIT_BINARY_DIR}/generated
)

# Propagate the compile-time log level to all example targets.
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=${_SPDLOG_ACTIVE_LEVEL})

# Shared example code (SDL media, wav source, etc.)
set(EXAMPLES_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)

# All example executables (used for copying livekit_ffi DLL/shared lib)
set(EXAMPLES_ALL
  SimpleRoom
  SimpleRpc
  SimpleJoystickSender
  SimpleJoystickReceiver
  SimpleDataStream
  LoggingLevelsBasicUsage
  LoggingLevelsCustomSinks
  HumanRobotRobot
  HumanRobotHuman
  MuteUnmuteCaller
  MuteUnmuteReceiver
  RpcCaller
  RpcReceiver
)

# Examples that use SDL3 (need SDL3 lib copied on Linux; SimpleRoom is handled separately above)
set(EXAMPLES_NEED_SDL3
  HumanRobotRobot
  HumanRobotHuman
  MuteUnmuteCaller
  MuteUnmuteReceiver
)

add_executable(SimpleRoom
  base_sdk/simple_room/main.cpp
  base_sdk/simple_room/fallback_capture.cpp
  base_sdk/simple_room/fallback_capture.h
  ${EXAMPLES_COMMON_DIR}/sdl_media.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.h
  ${EXAMPLES_COMMON_DIR}/sdl_media_manager.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media_manager.h
  ${EXAMPLES_COMMON_DIR}/sdl_video_renderer.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_video_renderer.h
  ${EXAMPLES_COMMON_DIR}/wav_audio_source.cpp
  ${EXAMPLES_COMMON_DIR}/wav_audio_source.h
)

target_include_directories(SimpleRoom PRIVATE
  ${EXAMPLES_PRIVATE_INCLUDE_DIRS}
  ${EXAMPLES_COMMON_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/base_sdk/simple_room
)

target_link_libraries(SimpleRoom
    PRIVATE
        livekit
        spdlog::spdlog
        SDL3::SDL3
)

add_custom_command(TARGET SimpleRoom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${LIVEKIT_ROOT_DIR}/data
            ${CMAKE_CURRENT_BINARY_DIR}/data
)

# Copy SDL3 shared library to SimpleRoom output directory
# On Linux, we also need to create the SONAME symlink (libSDL3.so.0 -> libSDL3.so.0.x.x)
# macOS doesn't need SONAME symlink (dylib versioning works differently)
if(UNIX AND NOT APPLE)
  add_custom_command(TARGET SimpleRoom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>"
            "$<TARGET_FILE_DIR:SimpleRoom>"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "$<TARGET_FILE_NAME:SDL3::SDL3>"
            "$<TARGET_FILE_DIR:SimpleRoom>/$<TARGET_SONAME_FILE_NAME:SDL3::SDL3>"
    COMMENT "Copying SDL3 shared library and SONAME symlink to SimpleRoom output directory"
    VERBATIM
  )
else()
  # Windows and macOS: just copy the library file
  add_custom_command(TARGET SimpleRoom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>"
            "$<TARGET_FILE_DIR:SimpleRoom>"
    COMMENT "Copying SDL3 shared library to SimpleRoom output directory"
    VERBATIM
  )
endif()

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

add_executable(SimpleRpc
  base_sdk/simple_rpc/main.cpp
)

target_include_directories(SimpleRpc PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})

target_link_libraries(SimpleRpc
  PRIVATE
    nlohmann_json::nlohmann_json
    livekit
    spdlog::spdlog
)

# --- SimpleJoystick example (sender + receiver executables with shared json_utils) ---

add_library(simple_joystick_json_utils STATIC
  base_sdk/simple_joystick/json_utils.cpp
  base_sdk/simple_joystick/json_utils.h
  base_sdk/simple_joystick/utils.cpp
  base_sdk/simple_joystick/utils.h
)

target_include_directories(simple_joystick_json_utils PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/base_sdk/simple_joystick
)

target_link_libraries(simple_joystick_json_utils
  PUBLIC
    nlohmann_json::nlohmann_json
)

add_executable(SimpleJoystickReceiver
  base_sdk/simple_joystick/receiver.cpp
)

target_include_directories(SimpleJoystickReceiver PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})

target_link_libraries(SimpleJoystickReceiver
  PRIVATE
    simple_joystick_json_utils
    livekit
    spdlog::spdlog
)

add_executable(SimpleJoystickSender
  base_sdk/simple_joystick/sender.cpp
)

target_include_directories(SimpleJoystickSender PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})

target_link_libraries(SimpleJoystickSender
  PRIVATE
    simple_joystick_json_utils
    livekit
    spdlog::spdlog
)

# --- LoggingLevelsBasicUsage example ---

add_executable(LoggingLevelsBasicUsage
  base_sdk/logging_levels/basic_usage.cpp
)

target_include_directories(LoggingLevelsBasicUsage PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})

target_link_libraries(LoggingLevelsBasicUsage
  PRIVATE
    livekit
    spdlog::spdlog
)

# --- LoggingLevelsCustomSinks example ---

add_executable(LoggingLevelsCustomSinks
  base_sdk/logging_levels/custom_sinks.cpp
)

target_link_libraries(LoggingLevelsCustomSinks
  PRIVATE
    livekit
)

# --- SimpleDataStream example ---

add_executable(SimpleDataStream
  base_sdk/simple_data_stream/main.cpp
)

target_include_directories(SimpleDataStream PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})

target_link_libraries(SimpleDataStream
  PRIVATE
    livekit
    spdlog::spdlog
)

add_custom_command(
  TARGET SimpleDataStream
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${LIVEKIT_ROOT_DIR}/data
          $<TARGET_FILE_DIR:SimpleDataStream>/data
)

# --- human_robot examples (robot + human; use session_manager and SDL3) ---

add_executable(HumanRobotRobot
  human_robot/robot.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.h
)
target_include_directories(HumanRobotRobot PRIVATE
  ${EXAMPLES_PRIVATE_INCLUDE_DIRS}
  ${EXAMPLES_COMMON_DIR}
)
target_link_libraries(HumanRobotRobot PRIVATE livekit spdlog::spdlog SDL3::SDL3)

add_executable(HumanRobotHuman
  human_robot/human.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.h
)
target_include_directories(HumanRobotHuman PRIVATE
  ${EXAMPLES_PRIVATE_INCLUDE_DIRS}
  ${EXAMPLES_COMMON_DIR}
)
target_link_libraries(HumanRobotHuman PRIVATE livekit spdlog::spdlog SDL3::SDL3)

# --- rpc examples (headless custom RPC caller + receiver) ---

add_executable(RpcCaller
  rpc/custom_caller.cpp
)
target_include_directories(RpcCaller PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})
target_link_libraries(RpcCaller PRIVATE livekit)

add_executable(RpcReceiver
  rpc/custom_receiver.cpp
)
target_include_directories(RpcReceiver PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})
target_link_libraries(RpcReceiver PRIVATE livekit)

# --- mute_unmute examples (caller uses SDL3 for A/V playback; receiver is headless) ---

add_executable(MuteUnmuteCaller
  mute_unmute/caller.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.h
)
target_include_directories(MuteUnmuteCaller PRIVATE
  ${EXAMPLES_PRIVATE_INCLUDE_DIRS}
  ${EXAMPLES_COMMON_DIR}
)
target_link_libraries(MuteUnmuteCaller PRIVATE livekit spdlog::spdlog SDL3::SDL3)

add_executable(MuteUnmuteReceiver
  mute_unmute/receiver.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.h
)
target_include_directories(MuteUnmuteReceiver PRIVATE
  ${EXAMPLES_PRIVATE_INCLUDE_DIRS}
  ${EXAMPLES_COMMON_DIR}
)
ßtarget_link_libraries(MuteUnmuteReceiver PRIVATE livekit spdlog::spdlog SDL3::SDL3)

# Copy SDL3 shared library to examples output directories
if(UNIX AND NOT APPLE)
  foreach(_target ${EXAMPLES_NEED_SDL3})
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
  endforeach()
else()
  foreach(_target ${EXAMPLES_NEED_SDL3})
    add_custom_command(TARGET ${_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:SDL3::SDL3>"
              "$<TARGET_FILE_DIR:${_target}>"
      COMMENT "Copying SDL3 shared library to ${_target} output directory"
      VERBATIM
    )
  endforeach()
endif()

# Windows: Copy required DLLs to examples output directory (single copy to avoid parallel POST_BUILD races)
if(WIN32)
  set(REQUIRED_DLLS "livekit_ffi.dll")

  # One copy of each DLL to bin/; all examples depend on this target
  add_custom_command(OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/livekit_ffi.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE_DIR:livekit>/livekit_ffi.dll
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/livekit_ffi.dll
    DEPENDS livekit
    COMMENT "Copying livekit_ffi.dll to examples output directory"
  )
  add_custom_target(copy_ffi_dll_to_bin ALL DEPENDS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/livekit_ffi.dll)
  foreach(EXAMPLE ${EXAMPLES_ALL})
    add_dependencies(${EXAMPLE} copy_ffi_dll_to_bin)
  endforeach()

endif()

# Linux/macOS: Copy shared library to examples output directory (single copy to avoid parallel POST_BUILD races)
if(UNIX)
  if(APPLE)
    set(FFI_SHARED_LIB "liblivekit_ffi.dylib")
  else()
    set(FFI_SHARED_LIB "liblivekit_ffi.so")
  endif()

  # One copy to bin/; all examples depend on this target so the copy runs once
  add_custom_command(OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${FFI_SHARED_LIB}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE_DIR:livekit>/${FFI_SHARED_LIB}
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${FFI_SHARED_LIB}
    DEPENDS livekit
    COMMENT "Copying ${FFI_SHARED_LIB} to examples output directory"
  )
  add_custom_target(copy_ffi_to_bin ALL DEPENDS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${FFI_SHARED_LIB})
  foreach(EXAMPLE ${EXAMPLES_ALL})
    add_dependencies(${EXAMPLE} copy_ffi_to_bin)
  endforeach()

endif()