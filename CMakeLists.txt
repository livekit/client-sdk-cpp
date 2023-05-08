cmake_minimum_required(VERSION 3.0)

project(livekit)

set(CMAKE_CXX_STANDARD 17)
set(FFI_PROTO_PATH client-sdk-rust/livekit-ffi/protocol)
set(FFI_PROTO_FILES
    ${FFI_PROTO_PATH}/ffi.proto
    ${FFI_PROTO_PATH}/handle.proto
    ${FFI_PROTO_PATH}/participant.proto
    ${FFI_PROTO_PATH}/room.proto
    ${FFI_PROTO_PATH}/track.proto
    ${FFI_PROTO_PATH}/video_frame.proto
    ${FFI_PROTO_PATH}/audio_frame.proto
)

find_package(Protobuf REQUIRED)

protobuf_generate_cpp(PROTO_SRCS PROTO_HEADERS ${FFI_PROTO_FILES})

file(GLOB_RECURSE SRCS src/*.cpp)
add_library(livekit ${SRCS} ${PROTO_SRCS} ${PROTO_HEADERS})

# Include the auto-generated files from livekit-ffi (C headers)
target_include_directories(livekit PRIVATE client-sdk-rust/livekit-ffi/include/)
target_include_directories(livekit PUBLIC include/)

# Used for protobuf
include_directories(livekit PRIVATE ${Protobuf_INCLUDE_DIRS})
include_directories(livekit PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Link against livekit-ffi
target_link_libraries(livekit PRIVATE livekit-ffi)

