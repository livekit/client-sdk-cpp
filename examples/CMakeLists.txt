cmake_minimum_required(VERSION 3.31.0)
project (livekit-examples)

add_executable(SimpleRoom
    simple_room/main.cpp
    simple_room/wav_audio_source.cpp
    simple_room/wav_audio_source.h
)

target_link_libraries(SimpleRoom livekit)

add_custom_command(TARGET SimpleRoom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/data
            ${CMAKE_CURRENT_BINARY_DIR}/data
)