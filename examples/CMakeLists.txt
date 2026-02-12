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
  BridgeRobot
  BridgeHuman
  BridgeMuteCaller
  BridgeMuteReceiver
  BridgeRpcCaller
  BridgeRpcReceiver
)

# Bridge examples (need livekit_bridge DLL/shared lib in addition to livekit_ffi)
set(EXAMPLES_BRIDGE
  BridgeRobot
  BridgeHuman
  BridgeMuteCaller
  BridgeMuteReceiver
  BridgeRpcCaller
  BridgeRpcReceiver
)

# Examples that use SDL3 (need SDL3 lib copied on Linux; SimpleRoom is handled separately above)
set(EXAMPLES_NEED_SDL3
  BridgeRobot
  BridgeHuman
  BridgeMuteCaller
  BridgeMuteReceiver
)

add_executable(SimpleRoom
  simple_room/main.cpp
  simple_room/fallback_capture.cpp
  simple_room/fallback_capture.h
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
  ${CMAKE_CURRENT_SOURCE_DIR}/simple_room
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
  simple_rpc/main.cpp
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
  simple_joystick/json_utils.cpp
  simple_joystick/json_utils.h
  simple_joystick/utils.cpp
  simple_joystick/utils.h
)

target_include_directories(simple_joystick_json_utils PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/simple_joystick
)

target_link_libraries(simple_joystick_json_utils
  PUBLIC
    nlohmann_json::nlohmann_json
)

add_executable(SimpleJoystickReceiver
  simple_joystick/receiver.cpp
)

target_include_directories(SimpleJoystickReceiver PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})

target_link_libraries(SimpleJoystickReceiver
  PRIVATE
    simple_joystick_json_utils
    livekit
    spdlog::spdlog
)

add_executable(SimpleJoystickSender
  simple_joystick/sender.cpp
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
  logging_levels/basic_usage.cpp
)

target_include_directories(LoggingLevelsBasicUsage PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})

target_link_libraries(LoggingLevelsBasicUsage
  PRIVATE
    livekit
    spdlog::spdlog
)

# --- LoggingLevelsCustomSinks example ---

add_executable(LoggingLevelsCustomSinks
  logging_levels/custom_sinks.cpp
)

target_link_libraries(LoggingLevelsCustomSinks
  PRIVATE
    livekit
)

# --- SimpleDataStream example ---

add_executable(SimpleDataStream
  simple_data_stream/main.cpp
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

# --- bridge_human_robot examples (robot + human; use livekit_bridge and SDL3) ---

add_executable(BridgeRobot
  bridge_human_robot/robot.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.h
)
target_include_directories(BridgeRobot PRIVATE
  ${EXAMPLES_PRIVATE_INCLUDE_DIRS}
  ${EXAMPLES_COMMON_DIR}
)
target_link_libraries(BridgeRobot PRIVATE livekit_bridge spdlog::spdlog SDL3::SDL3)

add_executable(BridgeHuman
  bridge_human_robot/human.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.h
)
target_include_directories(BridgeHuman PRIVATE
  ${EXAMPLES_PRIVATE_INCLUDE_DIRS}
  ${EXAMPLES_COMMON_DIR}
)
target_link_libraries(BridgeHuman PRIVATE livekit_bridge spdlog::spdlog SDL3::SDL3)

# --- bridge_rpc examples (headless custom RPC caller + receiver) ---

add_executable(BridgeRpcCaller
  bridge_rpc/custom_caller.cpp
)
target_link_libraries(BridgeRpcCaller PRIVATE livekit_bridge)

add_executable(BridgeRpcReceiver
  bridge_rpc/custom_receiver.cpp
)
target_link_libraries(BridgeRpcReceiver PRIVATE livekit_bridge)

# --- bridge_mute_unmute examples (caller uses SDL3 for A/V playback; receiver is headless) ---

add_executable(BridgeMuteCaller
  bridge_mute_unmute/caller.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.h
)
target_include_directories(BridgeMuteCaller PRIVATE
  ${EXAMPLES_PRIVATE_INCLUDE_DIRS}
  ${EXAMPLES_COMMON_DIR}
)
target_link_libraries(BridgeMuteCaller PRIVATE livekit_bridge spdlog::spdlog SDL3::SDL3)

add_executable(BridgeMuteReceiver
  bridge_mute_unmute/receiver.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.cpp
  ${EXAMPLES_COMMON_DIR}/sdl_media.h
)
target_include_directories(BridgeMuteReceiver PRIVATE
  ${EXAMPLES_PRIVATE_INCLUDE_DIRS}
  ${EXAMPLES_COMMON_DIR}
)
target_link_libraries(BridgeMuteReceiver PRIVATE livekit_bridge spdlog::spdlog SDL3::SDL3)

# Copy SDL3 shared library to bridge example output directories
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

  # Bridge examples also need livekit_bridge.dll (single copy to bin/)
  set(BRIDGE_DLL "livekit_bridge.dll")
  add_custom_command(OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${BRIDGE_DLL}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:livekit_bridge>
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${BRIDGE_DLL}
    DEPENDS livekit_bridge
    COMMENT "Copying livekit_bridge DLL to examples output directory"
  )
  add_custom_target(copy_bridge_dll_to_bin ALL DEPENDS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${BRIDGE_DLL})
  foreach(EXAMPLE ${EXAMPLES_BRIDGE})
    add_dependencies(${EXAMPLE} copy_bridge_dll_to_bin)
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

  # Bridge examples also need livekit_bridge shared library (single copy to bin/)
  if(APPLE)
    set(BRIDGE_SHARED_LIB "liblivekit_bridge.dylib")
  else()
    set(BRIDGE_SHARED_LIB "liblivekit_bridge.so")
  endif()
  add_custom_command(OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${BRIDGE_SHARED_LIB}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:livekit_bridge>
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${BRIDGE_SHARED_LIB}
    DEPENDS livekit_bridge
    COMMENT "Copying livekit_bridge to examples output directory"
  )
  add_custom_target(copy_bridge_to_bin ALL DEPENDS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${BRIDGE_SHARED_LIB})
  foreach(EXAMPLE ${EXAMPLES_BRIDGE})
    add_dependencies(${EXAMPLE} copy_bridge_to_bin)
  endforeach()
endif()