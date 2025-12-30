cmake_minimum_required(VERSION 3.31.0)
project(livekit-examples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.0
)
FetchContent_MakeAvailable(SDL3)

function(livekit_windows_link_deps target)
  if (WIN32)
    target_link_libraries(${target} PRIVATE
      ntdll
      userenv
      winmm
      iphlpapi
      ole32
      uuid
      msdmo
      dmoguids
      strmiids
      wmcodecdspuuid
      ws2_32
      bcrypt
    )
  endif()
endfunction()

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

target_link_libraries(SimpleRoom PRIVATE livekit SDL3::SDL3)
livekit_windows_link_deps(SimpleRoom)

add_custom_command(TARGET SimpleRoom POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/data
          ${CMAKE_CURRENT_BINARY_DIR}/data
)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(SimpleRpc simple_rpc/main.cpp)
target_link_libraries(SimpleRpc PRIVATE nlohmann_json::nlohmann_json livekit)
livekit_windows_link_deps(SimpleRpc)

add_executable(SimpleDataStream simple_data_stream/main.cpp)
target_link_libraries(SimpleDataStream PRIVATE livekit)
livekit_windows_link_deps(SimpleDataStream)

add_custom_command(TARGET SimpleDataStream POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/data
          $<TARGET_FILE_DIR:SimpleDataStream>/data
)

if (WIN32 AND TARGET SDL3::SDL3)
  foreach(tgt SimpleRoom SimpleRpc SimpleDataStream)
    add_custom_command(TARGET ${tgt} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:SDL3::SDL3>
              $<TARGET_FILE_DIR:${tgt}>
    )
  endforeach()
endif()