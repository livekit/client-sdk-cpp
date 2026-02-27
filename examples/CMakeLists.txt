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

# --- SimpleRobot example (robot + human executables with shared json_utils) ---

add_library(simple_robot_json_utils STATIC
  simple_robot/json_utils.cpp
  simple_robot/json_utils.h
  simple_robot/utils.cpp
  simple_robot/utils.h
)

target_include_directories(simple_robot_json_utils PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/simple_robot
)

target_link_libraries(simple_robot_json_utils
  PUBLIC
    nlohmann_json::nlohmann_json
)

add_executable(SimpleRobot
  simple_robot/robot.cpp
)

target_include_directories(SimpleRobot PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})

target_link_libraries(SimpleRobot
  PRIVATE
    simple_robot_json_utils
    livekit
    spdlog::spdlog
)

add_executable(SimpleHuman
  simple_robot/human.cpp
)

target_include_directories(SimpleHuman PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})

target_link_libraries(SimpleHuman
  PRIVATE
    simple_robot_json_utils
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

# Copy SDL3 shared library to bridge_human_robot example output directories
if(UNIX AND NOT APPLE)
  foreach(_target BridgeRobot BridgeHuman)
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
  foreach(_target BridgeRobot BridgeHuman)
    add_custom_command(TARGET ${_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:SDL3::SDL3>"
              "$<TARGET_FILE_DIR:${_target}>"
      COMMENT "Copying SDL3 shared library to ${_target} output directory"
      VERBATIM
    )
  endforeach()
endif()

# Windows: Copy required DLLs to examples output directory
if(WIN32)
  # Get the livekit library output directory (where DLLs are copied during main build)
  set(LIVEKIT_LIB_DIR $<TARGET_FILE_DIR:livekit>)
  
  # List of DLLs to copy (using generator expressions for config-dependent names)
  set(REQUIRED_DLLS
    "livekit_ffi.dll"
  )
  
  # Copy DLLs to each example's output directory
  foreach(EXAMPLE SimpleRoom SimpleRpc SimpleRobot SimpleHuman SimpleDataStream LoggingLevelsBasicUsage LoggingLevelsCustomSinks BridgeRobot BridgeHuman)
    foreach(DLL ${REQUIRED_DLLS})
      add_custom_command(TARGET ${EXAMPLE} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIVEKIT_LIB_DIR}/${DLL}"
                "$<TARGET_FILE_DIR:${EXAMPLE}>/${DLL}"
        COMMENT "Copying ${DLL} to ${EXAMPLE} output directory"
        VERBATIM
      )
    endforeach()
  endforeach()

  # bridge_human_robot examples also need livekit_bridge.dll
  foreach(EXAMPLE BridgeRobot BridgeHuman)
    add_custom_command(TARGET ${EXAMPLE} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:livekit_bridge>"
              "$<TARGET_FILE_DIR:${EXAMPLE}>/$<TARGET_FILE_NAME:livekit_bridge>"
      COMMENT "Copying livekit_bridge DLL to ${EXAMPLE} output directory"
      VERBATIM
    )
  endforeach()
endif()

# Linux/macOS: Copy shared library to examples output directory
if(UNIX)
  set(LIVEKIT_LIB_DIR $<TARGET_FILE_DIR:livekit>)
  
  if(APPLE)
    set(FFI_SHARED_LIB "liblivekit_ffi.dylib")
  else()
    set(FFI_SHARED_LIB "liblivekit_ffi.so")
  endif()
  
  # Copy shared library to each example's output directory
  foreach(EXAMPLE SimpleRoom SimpleRpc SimpleRobot SimpleHuman SimpleDataStream LoggingLevelsBasicUsage LoggingLevelsCustomSinks BridgeRobot BridgeHuman)
    add_custom_command(TARGET ${EXAMPLE} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${LIVEKIT_LIB_DIR}/${FFI_SHARED_LIB}"
              "$<TARGET_FILE_DIR:${EXAMPLE}>/${FFI_SHARED_LIB}"
      COMMENT "Copying ${FFI_SHARED_LIB} to ${EXAMPLE} output directory"
      VERBATIM
    )
  endforeach()

  # bridge_human_robot examples also need livekit_bridge shared library
  foreach(EXAMPLE BridgeRobot BridgeHuman)
    add_custom_command(TARGET ${EXAMPLE} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:livekit_bridge>"
              "$<TARGET_FILE_DIR:${EXAMPLE}>/$<TARGET_FILE_NAME:livekit_bridge>"
      COMMENT "Copying livekit_bridge to ${EXAMPLE} output directory"
      VERBATIM
    )
  endforeach()
endif()