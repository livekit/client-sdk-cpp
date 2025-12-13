cmake_minimum_required(VERSION 3.31.0)
project (livekit-examples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#################### SimpleRoom example ##########################

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(sdl3)

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

target_link_libraries(SimpleRoom
    PRIVATE
        livekit
        SDL3::SDL3
)

add_custom_command(TARGET SimpleRoom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/data
            ${CMAKE_CURRENT_BINARY_DIR}/data
)

#################### SimpleRpc example ##########################

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(SimpleRpc
  simple_rpc/main.cpp
)

target_link_libraries(SimpleRpc
  PRIVATE
    nlohmann_json::nlohmann_json
    livekit
)

#################### SimpleDataStream example ##########################

add_executable(SimpleDataStream
  simple_data_stream/main.cpp
)

target_link_libraries(SimpleDataStream
  PRIVATE
    livekit
)

add_custom_command(
  TARGET SimpleDataStream
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/data
          $<TARGET_FILE_DIR:SimpleDataStream>/data
)