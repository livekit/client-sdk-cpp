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

add_library(livekit 
    include/livekit/room.h
    include/livekit/ffi_client.h
    include/livekit/livekit.h
    src/ffi_client.cpp
    src/room.cpp
    ${PROTO_SRCS} 
    ${PROTO_HEADERS}
)

# Include the auto-generated files from livekit-ffi (C headers)
target_include_directories(livekit PUBLIC client-sdk-rust/livekit-ffi/include/)
target_include_directories(livekit PUBLIC include/)

# Used for protobuf
target_include_directories(livekit PUBLIC ${Protobuf_INCLUDE_DIRS})
target_include_directories(livekit PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

# Link against livekit-ffi
link_directories(${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(livekit PUBLIC ${PROTOBUF_LIBRARY} livekit_ffi)

# Examples
add_subdirectory(examples)
