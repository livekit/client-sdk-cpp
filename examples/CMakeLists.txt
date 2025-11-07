cmake_minimum_required(VERSION 3.31.0)
project (livekit-examples)

add_executable(SimpleRoom simple_room/main.cpp)
target_link_libraries(SimpleRoom livekit)
