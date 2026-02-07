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

add_executable(SimpleRoom
  simple_room/main.cpp
  simple_room/fallback_capture.cpp
  simple_room/fallback_capture.h
  simple_room/sdl_media.cpp
  simple_room/sdl_media.h
  simple_room/sdl_media_manager.cpp
  simple_room/sdl_media_manager.h
  simple_room/sdl_video_renderer.cpp
  simple_room/sdl_video_renderer.h
  simple_room/wav_audio_source.cpp
  simple_room/wav_audio_source.h
)

target_include_directories(SimpleRoom PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})

target_link_libraries(SimpleRoom
    PRIVATE
        livekit
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
)

add_executable(SimpleDataStream
  simple_data_stream/main.cpp
)

target_include_directories(SimpleDataStream PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})

target_link_libraries(SimpleDataStream
  PRIVATE
    livekit
)

add_executable(PublishNv12TcpSource
  publish_nv12_tcp_source/main.cpp
  publish_nv12_tcp_source/raw_nv12_tcp_source.cpp
  publish_nv12_tcp_source/raw_nv12_tcp_source.h
)

target_include_directories(PublishNv12TcpSource PRIVATE ${EXAMPLES_PRIVATE_INCLUDE_DIRS})

target_link_libraries(PublishNv12TcpSource
  PRIVATE
    livekit
)

add_custom_command(
  TARGET SimpleDataStream
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${LIVEKIT_ROOT_DIR}/data
          $<TARGET_FILE_DIR:SimpleDataStream>/data
)

# Windows: Copy required DLLs to examples output directory
if(WIN32)
  # Get the livekit library output directory (where DLLs are copied during main build)
  set(LIVEKIT_LIB_DIR $<TARGET_FILE_DIR:livekit>)
  
  # List of DLLs to copy (using generator expressions for config-dependent names)
  set(REQUIRED_DLLS
    "livekit_ffi.dll"
  )
  
  # Copy DLLs to each example's output directory
  foreach(EXAMPLE SimpleRoom SimpleRpc SimpleDataStream PublishNv12TcpSource)
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
  foreach(EXAMPLE SimpleRoom SimpleRpc SimpleDataStream PublishNv12TcpSource)
    add_custom_command(TARGET ${EXAMPLE} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${LIVEKIT_LIB_DIR}/${FFI_SHARED_LIB}"
              "$<TARGET_FILE_DIR:${EXAMPLE}>/${FFI_SHARED_LIB}"
      COMMENT "Copying ${FFI_SHARED_LIB} to ${EXAMPLE} output directory"
      VERBATIM
    )
  endforeach()
endif()