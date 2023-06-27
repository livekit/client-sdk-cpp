cmake_minimum_required(VERSION 3.0)
project(livekit)

set(CMAKE_CXX_STANDARD 17)
set(FFI_PROTO_PATH client-sdk-rust/livekit-ffi/protocol)
set(FFI_PROTO_FILES
    ${FFI_PROTO_PATH}/handle.proto
    ${FFI_PROTO_PATH}/ffi.proto
    ${FFI_PROTO_PATH}/participant.proto
    ${FFI_PROTO_PATH}/room.proto
    ${FFI_PROTO_PATH}/track.proto
    ${FFI_PROTO_PATH}/video_frame.proto
    ${FFI_PROTO_PATH}/audio_frame.proto
)
set(PROTO_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_BINARY_DIR})

find_package(Protobuf REQUIRED)

# livekit-proto
add_library(livekit_proto OBJECT ${FFI_PROTO_FILES})
target_link_libraries(livekit_proto PUBLIC protobuf::libprotobuf)
target_include_directories(livekit_proto PUBLIC "$<BUILD_INTERFACE:${PROTO_BINARY_DIR}>")
target_include_directories(livekit_proto PUBLIC ${Protobuf_INCLUDE_DIRS})

protobuf_generate(
    TARGET livekit_proto
    PROTOS ${FFI_PROTO_FILES} 
    APPEND_PATH ${FFI_PROTO_PATH}
    IMPORT_DIRS ${FFI_PROTO_PATH}
    PROTOC_OUT_DIR ${PROTO_BINARY_DIR}
)

# livekit
add_library(livekit
    include/livekit/room.h
    include/livekit/ffi_client.h
    include/livekit/livekit.h
    include/livekit/video_frame.h
    include/livekit/track.h
    include/livekit/video_source.h
    include/livekit/participant.h
    src/ffi_client.cpp
    src/room.cpp
    src/video_frame.cpp
    src/track.cpp
    src/video_source.cpp
    src/participant.cpp
    ${PROTO_SRCS} 
    ${PROTO_HEADERS}
    ${PROTO_FILES}
)


# Include the auto-generated files from livekit-ffi (C headers)
target_include_directories(livekit PUBLIC client-sdk-rust/livekit-ffi/include/)
target_include_directories(livekit PUBLIC include/)

# Link against livekit-ffi
link_directories(${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(livekit PUBLIC livekit_ffi livekit_proto)

# Examples
add_subdirectory(examples)
