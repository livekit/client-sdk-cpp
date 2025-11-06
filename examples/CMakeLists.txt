cmake_minimum_required(VERSION 4.1)
project (livekit-examples)

add_executable(SimpleRoom simple_room/main.cpp)
target_link_libraries(SimpleRoom livekit)
