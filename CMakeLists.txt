cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED LIVEKIT_VERSION OR LIVEKIT_VERSION STREQUAL "")
  set(LIVEKIT_VERSION "0.0.0")
endif()

project(livekit VERSION ${LIVEKIT_VERSION} LANGUAGES C CXX)

set(LIVEKIT_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LIVEKIT_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(LIVEKIT_BUILD_EXAMPLES "Build LiveKit examples" OFF)

# vcpkg is only used on Windows; Linux/macOS use system package managers
if(WIN32)
  option(LIVEKIT_USE_VCPKG "Use vcpkg for dependency management (Windows only)" ON)
  set(LIVEKIT_USE_SYSTEM_PROTOBUF ON CACHE BOOL "Use system protobuf (vcpkg) on Windows" FORCE)
  # Force /MD or /MDd for ALL targets (including FetchContent subprojects)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)
  add_compile_options(/utf-8)
else()
  option(LIVEKIT_USE_VCPKG "Use vcpkg for dependency management" OFF)
  set(LIVEKIT_USE_SYSTEM_PROTOBUF OFF CACHE BOOL "Use vendored protobuf on non-Windows" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set RPATH for Unix systems to find shared libraries in executable directory
if(UNIX)
  # Use $ORIGIN on Linux, @executable_path on macOS
  if(APPLE)
    set(CMAKE_BUILD_RPATH "@executable_path")
    set(CMAKE_INSTALL_RPATH "@executable_path")
  else()
    set(CMAKE_BUILD_RPATH "$ORIGIN")
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
  endif()
  set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
endif()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(LIVEKIT_IS_TOPLEVEL TRUE)
else()
  set(LIVEKIT_IS_TOPLEVEL FALSE)
endif()

if(LIVEKIT_IS_TOPLEVEL)
  # Since we use separate build directories (build-debug/build-release),
  # we don't need additional platform/config subdirectories
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  
  # For multi-config generators (Visual Studio), override per-config directories
  # to prevent adding Debug/Release subdirectories
  foreach(CONFIG_TYPE Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/bin)
  endforeach()
endif()

set(FFI_PROTO_DIR ${LIVEKIT_ROOT_DIR}/client-sdk-rust/livekit-ffi/protocol)
set(FFI_PROTO_FILES
    ${FFI_PROTO_DIR}/handle.proto
    ${FFI_PROTO_DIR}/ffi.proto
    ${FFI_PROTO_DIR}/participant.proto
    ${FFI_PROTO_DIR}/room.proto
    ${FFI_PROTO_DIR}/track.proto
    ${FFI_PROTO_DIR}/video_frame.proto
    ${FFI_PROTO_DIR}/audio_frame.proto
    ${FFI_PROTO_DIR}/e2ee.proto
    ${FFI_PROTO_DIR}/stats.proto
    ${FFI_PROTO_DIR}/data_stream.proto
    ${FFI_PROTO_DIR}/rpc.proto
    ${FFI_PROTO_DIR}/track_publication.proto
)
set(PROTO_BINARY_DIR ${LIVEKIT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_BINARY_DIR})

# Livekit static protobuf.
include(protobuf)
# Ensure protoc executable is found.
if(TARGET protobuf::protoc)
  set(Protobuf_PROTOC_EXECUTABLE "$<TARGET_FILE:protobuf::protoc>")
elseif(NOT Protobuf_PROTOC_EXECUTABLE)
  find_program(Protobuf_PROTOC_EXECUTABLE NAMES protoc REQUIRED)
endif()
message(STATUS "Using protoc: ${Protobuf_PROTOC_EXECUTABLE}")

add_library(livekit_proto OBJECT ${FFI_PROTO_FILES})
if(TARGET protobuf::libprotobuf)
  set(LIVEKIT_PROTOBUF_TARGET protobuf::libprotobuf)
else()
  message(FATAL_ERROR "No protobuf library target found (expected protobuf::libprotobuf)")
endif()
target_include_directories(livekit_proto PRIVATE
  "${PROTO_BINARY_DIR}"
  ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(livekit_proto PRIVATE ${LIVEKIT_PROTOBUF_TARGET})
if(TARGET absl::base)
  get_target_property(_absl_inc absl::base INTERFACE_INCLUDE_DIRECTORIES)
  if(_absl_inc)
    target_include_directories(livekit_proto PRIVATE ${_absl_inc})
  endif()
endif()

# Manually generate protobuf files to avoid path prefix issues
set(PROTO_SRCS)
set(PROTO_HDRS)
foreach(PROTO_FILE ${FFI_PROTO_FILES})
  get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
  list(APPEND PROTO_SRCS "${PROTO_BINARY_DIR}/${PROTO_NAME}.pb.cc")
  list(APPEND PROTO_HDRS "${PROTO_BINARY_DIR}/${PROTO_NAME}.pb.h")
endforeach()

add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
  COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    --proto_path=${FFI_PROTO_DIR}
    --cpp_out=${PROTO_BINARY_DIR}
    ${FFI_PROTO_FILES}
  DEPENDS ${FFI_PROTO_FILES}
  COMMENT "Generating C++ protobuf files"
  VERBATIM
)

target_sources(livekit_proto PRIVATE ${PROTO_SRCS} ${PROTO_HDRS})

set(GENERATED_INCLUDE_DIR "${LIVEKIT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_INCLUDE_DIR}")

set(GIT_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${LIVEKIT_ROOT_DIR}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
    WORKING_DIRECTORY "${LIVEKIT_ROOT_DIR}"
    OUTPUT_VARIABLE GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()

string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M:%S")
set(GENERATED_COMMENT "This file was auto-generated by CMake on ${BUILD_DATE}. Do NOT edit manually.")

configure_file(
  "${LIVEKIT_ROOT_DIR}/build.h.in"
  "${GENERATED_INCLUDE_DIR}/build.h"
  @ONLY
)

find_program(CARGO_EXECUTABLE NAMES cargo REQUIRED)

set(RUST_ROOT ${LIVEKIT_ROOT_DIR}/client-sdk-rust)

set(RUN_CARGO_SCRIPT ${LIVEKIT_BINARY_DIR}/run_cargo.cmake)
file(WRITE ${RUN_CARGO_SCRIPT}
"if(NOT DEFINED CFG)
  set(CFG Debug)
endif()
if(NOT DEFINED RUST_ROOT)
  message(FATAL_ERROR \"RUST_ROOT not set\")
endif()
if(NOT DEFINED CARGO)
  message(FATAL_ERROR \"CARGO not set\")
endif()

set(ARGS build)
if(NOT CFG STREQUAL \"Debug\")
  list(APPEND ARGS --release)
endif()

message(STATUS \"[run_cargo.cmake] CFG=\${CFG}  CARGO=\${CARGO}  PROTOC=\${PROTOC_PATH}\")
set(ENV{PROTOC} \"\${PROTOC_PATH}\")

execute_process(
  COMMAND \"\${CARGO}\" \${ARGS}
  WORKING_DIRECTORY \"\${RUST_ROOT}\"
  RESULT_VARIABLE rv
)
if(rv)
  message(FATAL_ERROR \"cargo build failed with code: \${rv}\")
endif()
")

# Use SHARED library (DLL) on Windows to isolate /MT CRT from consuming /MD apps
if(WIN32)
  add_library(livekit_ffi SHARED IMPORTED GLOBAL)
  # DLL import libraries (.dll.lib)
  set(RUST_IMPLIB_DEBUG   "${RUST_ROOT}/target/debug/livekit_ffi.dll.lib")
  set(RUST_IMPLIB_RELEASE "${RUST_ROOT}/target/release/livekit_ffi.dll.lib")
  # Actual DLL files
  set(RUST_DLL_DEBUG      "${RUST_ROOT}/target/debug/livekit_ffi.dll")
  set(RUST_DLL_RELEASE    "${RUST_ROOT}/target/release/livekit_ffi.dll")
  # For OUTPUT dependency checking
  set(RUST_LIB_DEBUG   "${RUST_IMPLIB_DEBUG}")
  set(RUST_LIB_RELEASE "${RUST_IMPLIB_RELEASE}")
  
  set_target_properties(livekit_ffi PROPERTIES
    IMPORTED_CONFIGURATIONS "Debug;Release;RelWithDebInfo;MinSizeRel"
    # IMPORTED_LOCATION = DLL file
    IMPORTED_LOCATION_DEBUG          "${RUST_DLL_DEBUG}"
    IMPORTED_LOCATION_RELEASE        "${RUST_DLL_RELEASE}"
    IMPORTED_LOCATION_RELWITHDEBINFO "${RUST_DLL_RELEASE}"
    IMPORTED_LOCATION_MINSIZEREL     "${RUST_DLL_RELEASE}"
    # IMPORTED_IMPLIB = Import library for linking
    IMPORTED_IMPLIB_DEBUG            "${RUST_IMPLIB_DEBUG}"
    IMPORTED_IMPLIB_RELEASE          "${RUST_IMPLIB_RELEASE}"
    IMPORTED_IMPLIB_RELWITHDEBINFO   "${RUST_IMPLIB_RELEASE}"
    IMPORTED_IMPLIB_MINSIZEREL       "${RUST_IMPLIB_RELEASE}"
    INTERFACE_INCLUDE_DIRECTORIES    "${RUST_ROOT}/livekit-ffi/include"
  )
elseif(APPLE)
  # macOS: use dynamic library (.dylib) to isolate Debug/Release ABI differences
  add_library(livekit_ffi SHARED IMPORTED GLOBAL)
  set(RUST_LIB_DEBUG   "${RUST_ROOT}/target/debug/liblivekit_ffi.dylib")
  set(RUST_LIB_RELEASE "${RUST_ROOT}/target/release/liblivekit_ffi.dylib")
  
  set_target_properties(livekit_ffi PROPERTIES
    IMPORTED_CONFIGURATIONS "Debug;Release;RelWithDebInfo;MinSizeRel"
    IMPORTED_LOCATION_DEBUG          "${RUST_LIB_DEBUG}"
    IMPORTED_LOCATION_RELEASE        "${RUST_LIB_RELEASE}"
    IMPORTED_LOCATION_RELWITHDEBINFO "${RUST_LIB_RELEASE}"
    IMPORTED_LOCATION_MINSIZEREL     "${RUST_LIB_RELEASE}"
    IMPORTED_NO_SONAME               TRUE
    INTERFACE_INCLUDE_DIRECTORIES    "${RUST_ROOT}/livekit-ffi/include"
  )
else()
  # Linux: use dynamic library (.so) to isolate Debug/Release ABI differences
  add_library(livekit_ffi SHARED IMPORTED GLOBAL)
  set(RUST_LIB_DEBUG   "${RUST_ROOT}/target/debug/liblivekit_ffi.so")
  set(RUST_LIB_RELEASE "${RUST_ROOT}/target/release/liblivekit_ffi.so")
  
  set_target_properties(livekit_ffi PROPERTIES
    IMPORTED_CONFIGURATIONS "Debug;Release;RelWithDebInfo;MinSizeRel"
    IMPORTED_LOCATION_DEBUG          "${RUST_LIB_DEBUG}"
    IMPORTED_LOCATION_RELEASE        "${RUST_LIB_RELEASE}"
    IMPORTED_LOCATION_RELWITHDEBINFO "${RUST_LIB_RELEASE}"
    IMPORTED_LOCATION_MINSIZEREL     "${RUST_LIB_RELEASE}"
    IMPORTED_NO_SONAME               TRUE
    INTERFACE_INCLUDE_DIRECTORIES    "${RUST_ROOT}/livekit-ffi/include"
  )
endif()

file(GLOB_RECURSE RUST_SOURCES 
  "${RUST_ROOT}/livekit-ffi/src/*.rs"
  "${RUST_ROOT}/livekit-ffi/Cargo.toml"
)

add_custom_command(
  OUTPUT "${RUST_LIB_DEBUG}" "${RUST_LIB_RELEASE}"
  COMMAND "${CMAKE_COMMAND}"
          -DCFG=$<CONFIG>
          -DRUST_ROOT=${RUST_ROOT}
          -DCARGO=${CARGO_EXECUTABLE}
          -DPROTOC_PATH=${Protobuf_PROTOC_EXECUTABLE}
          -P "${RUN_CARGO_SCRIPT}"
  WORKING_DIRECTORY "${RUST_ROOT}"
  DEPENDS ${RUST_SOURCES}
  COMMENT "Building Rust FFI via cargo"
  VERBATIM
)

add_custom_target(build_rust_ffi
  DEPENDS "${RUST_LIB_DEBUG}" "${RUST_LIB_RELEASE}"
)

# Note: protozero_plugin.o removal is no longer needed since we use dynamic libraries on Unix

add_library(livekit SHARED
  src/audio_frame.cpp
  src/audio_source.cpp
  src/audio_stream.cpp
  src/data_stream.cpp
  src/e2ee.cpp
  src/ffi_handle.cpp
  src/ffi_client.cpp
  src/ffi_client.h
  src/livekit.cpp
  src/local_audio_track.cpp
  src/remote_audio_track.cpp
  src/room.cpp
  src/room_proto_converter.cpp
  src/room_proto_converter.h
  src/local_participant.cpp
  src/remote_participant.cpp
  src/stats.cpp
  src/track.cpp
  src/track_proto_converter.cpp
  src/track_proto_converter.h
  src/track_publication.cpp
  src/local_track_publication.cpp
  src/remote_track_publication.cpp
  src/rpc_error.cpp
  src/video_frame.cpp
  src/video_source.cpp
  src/video_stream.cpp
  src/local_video_track.cpp
  src/remote_video_track.cpp
  src/video_utils.cpp
  src/video_utils.h
)

target_sources(livekit PRIVATE $<TARGET_OBJECTS:livekit_proto>)

target_include_directories(livekit
  PUBLIC
    $<BUILD_INTERFACE:${LIVEKIT_ROOT_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${LIVEKIT_ROOT_DIR}/src
    ${RUST_ROOT}/livekit-ffi/include
    ${PROTO_BINARY_DIR}
    ${GENERATED_INCLUDE_DIR}
)

target_link_libraries(livekit
  PRIVATE
    livekit_ffi
    ${LIVEKIT_PROTOBUF_TARGET}
)

message(STATUS "Protobuf: version=${Protobuf_VERSION}; protoc=${Protobuf_PROTOC_EXECUTABLE}")

add_dependencies(livekit build_rust_ffi)

if(LIVEKIT_IS_TOPLEVEL)
  # Copy public headers to build/include
  add_custom_command(
    TARGET livekit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${LIVEKIT_ROOT_DIR}/include
            ${CMAKE_BINARY_DIR}/include
    COMMAND ${CMAKE_COMMAND} -E copy
            ${GENERATED_INCLUDE_DIR}/build.h
            ${CMAKE_BINARY_DIR}/include/livekit/build.h
    COMMENT "Copying public headers to build/include"
  )

  # Copy livekit_ffi library to output directory alongside livekit library
  if(WIN32)
    # Copy both DLL and import library
    set(FFI_DLL_DEBUG     "${RUST_ROOT}/target/debug/livekit_ffi.dll")
    set(FFI_DLL_RELEASE   "${RUST_ROOT}/target/release/livekit_ffi.dll")
    set(FFI_IMPLIB_DEBUG  "${RUST_ROOT}/target/debug/livekit_ffi.dll.lib")
    set(FFI_IMPLIB_RELEASE "${RUST_ROOT}/target/release/livekit_ffi.dll.lib")
    
    # Copy DLL file
    add_custom_command(
      TARGET livekit POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy
              "$<IF:$<CONFIG:Debug>,${FFI_DLL_DEBUG},${FFI_DLL_RELEASE}>"
              "$<TARGET_FILE_DIR:livekit>/livekit_ffi.dll"
      COMMENT "Copying livekit_ffi.dll to output directory"
    )
    # Copy import library
    add_custom_command(
      TARGET livekit POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy
              "$<IF:$<CONFIG:Debug>,${FFI_IMPLIB_DEBUG},${FFI_IMPLIB_RELEASE}>"
              "$<TARGET_FILE_DIR:livekit>/livekit_ffi.dll.lib"
      COMMENT "Copying livekit_ffi.dll.lib to output directory"
    )
  elseif(APPLE)
    # macOS: copy .dylib
    set(FFI_LIB_DEBUG   "${RUST_ROOT}/target/debug/liblivekit_ffi.dylib")
    set(FFI_LIB_RELEASE "${RUST_ROOT}/target/release/liblivekit_ffi.dylib")
    add_custom_command(
      TARGET livekit POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy
              "$<IF:$<CONFIG:Debug>,${FFI_LIB_DEBUG},${FFI_LIB_RELEASE}>"
              "$<TARGET_FILE_DIR:livekit>/liblivekit_ffi.dylib"
      COMMENT "Copying liblivekit_ffi.dylib to output directory"
    )
  else()
    # Linux: copy .so
    set(FFI_LIB_DEBUG   "${RUST_ROOT}/target/debug/liblivekit_ffi.so")
    set(FFI_LIB_RELEASE "${RUST_ROOT}/target/release/liblivekit_ffi.so")
    add_custom_command(
      TARGET livekit POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy
              "$<IF:$<CONFIG:Debug>,${FFI_LIB_DEBUG},${FFI_LIB_RELEASE}>"
              "$<TARGET_FILE_DIR:livekit>/liblivekit_ffi.so"
      COMMENT "Copying liblivekit_ffi.so to output directory"
    )
  endif()
endif()

if(APPLE)
  if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "12.3")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.3" CACHE STRING "Minimum macOS version" FORCE)
  endif()

  find_library(FW_COREAUDIO      CoreAudio      REQUIRED)
  find_library(FW_AUDIOTOOLBOX   AudioToolbox   REQUIRED)
  find_library(FW_COREFOUNDATION CoreFoundation REQUIRED)
  find_library(FW_SECURITY       Security       REQUIRED)
  find_library(FW_COREGRAPHICS   CoreGraphics   REQUIRED)
  find_library(FW_COREMEDIA      CoreMedia      REQUIRED)
  find_library(FW_VIDEOTOOLBOX   VideoToolbox   REQUIRED)
  find_library(FW_AVFOUNDATION   AVFoundation   REQUIRED)
  find_library(FW_COREVIDEO      CoreVideo      REQUIRED)
  find_library(FW_FOUNDATION     Foundation     REQUIRED)
  find_library(FW_APPKIT         AppKit         REQUIRED)
  find_library(FW_QUARTZCORE     QuartzCore     REQUIRED)
  find_library(FW_OPENGL         OpenGL         REQUIRED)
  find_library(FW_IOSURFACE      IOSurface      REQUIRED)
  find_library(FW_METAL          Metal          REQUIRED)
  find_library(FW_METALKIT       MetalKit       REQUIRED)
  find_library(FW_SCREENCAPTUREKIT ScreenCaptureKit REQUIRED)

  target_link_libraries(livekit PRIVATE
    ${FW_COREAUDIO}
    ${FW_AUDIOTOOLBOX}
    ${FW_COREFOUNDATION}
    ${FW_SECURITY}
    ${FW_COREGRAPHICS}
    ${FW_COREMEDIA}
    ${FW_VIDEOTOOLBOX}
    ${FW_AVFOUNDATION}
    ${FW_COREVIDEO}
    ${FW_FOUNDATION}
    ${FW_APPKIT}
    ${FW_QUARTZCORE}
    ${FW_OPENGL}
    ${FW_IOSURFACE}
    ${FW_METAL}
    ${FW_METALKIT}
    ${FW_SCREENCAPTUREKIT}
  )

  target_link_options(livekit PRIVATE "LINKER:-ObjC")
endif()

if(WIN32)
  target_link_libraries(livekit PRIVATE
    ntdll
    userenv
    winmm
    iphlpapi
    msdmo
    dmoguids
    wmcodecdspuuid
    ws2_32
    secur32
    bcrypt
    crypt32
  )
endif()

if(UNIX AND NOT APPLE)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(livekit PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

if(MSVC)
  target_compile_options(livekit PRIVATE /permissive- /Zc:__cplusplus /W4)
else()
  target_compile_options(livekit PRIVATE -Wall -Wextra -Wpedantic)
endif()

# -------------------- Install / Package (SDK bundle) --------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(LIVEKIT_PACKAGE_NAME "LiveKit")
set(LIVEKIT_EXPORT_NAMESPACE "LiveKit::")

# Install the library target
install(TARGETS livekit
  EXPORT LiveKitTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}   # Windows .dll
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}   # .so/.dylib
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}   # .a/.lib
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
# Runtime artifacts (DLL / .so / .dylib) for imported shared libs.
# On Unix, the shared library is also the link-time artifact, so this is enough.
install(IMPORTED_RUNTIME_ARTIFACTS livekit_ffi
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
# Windows needs the import libraries (.dll.lib/.lib) for link-time.
# Note: we build either Debug OR Release per build (./build.sh debug or ./build.sh release),
# so we only install the corresponding configuration’s import libs.
if(WIN32)
  # livekit_ffi import lib
  if(TARGET livekit_ffi)
    install(FILES
      $<TARGET_PROPERTY:livekit_ffi,IMPORTED_IMPLIB_RELEASE>
      DESTINATION ${CMAKE_INSTALL_LIBDIR}
      CONFIGURATIONS Release RelWithDebInfo MinSizeRel
    )
    install(FILES
      $<TARGET_PROPERTY:livekit_ffi,IMPORTED_IMPLIB_DEBUG>
      DESTINATION ${CMAKE_INSTALL_LIBDIR}
      CONFIGURATIONS Debug
    )
  endif()
endif()

# Install public headers
install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Install generated build.h if it is part of the public headers
install(FILES "${GENERATED_INCLUDE_DIR}/build.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/livekit"
)

# ---- Create Config + Version + Targets for find_package(LiveKit CONFIG) ----

# cmake/LiveKitConfig.cmake.in must exist in your repo
configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/cmake/LiveKitConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/LiveKitConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${LIVEKIT_PACKAGE_NAME}"
)

# Generate a version file automatically from project(VERSION ...)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/LiveKitConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

# Export targets
install(EXPORT LiveKitTargets
  FILE LiveKitTargets.cmake
  NAMESPACE ${LIVEKIT_EXPORT_NAMESPACE}
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${LIVEKIT_PACKAGE_NAME}"
)

# Install config files
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/LiveKitConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/LiveKitConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${LIVEKIT_PACKAGE_NAME}"
)

# ------------------------------------------------------------------------

# ---- Examples ----
# add_subdirectory(examples)


if(LIVEKIT_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

add_custom_target(clean_generated
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${PROTO_BINARY_DIR}"
  COMMENT "Clean generated protobuf files"
  VERBATIM
)

add_custom_target(cargo_clean
  COMMAND "${CARGO_EXECUTABLE}" clean
  WORKING_DIRECTORY "${RUST_ROOT}"
  COMMENT "Clean Rust target directory via cargo"
  VERBATIM
)

add_custom_target(clean_all
  # 1) CMake clean (object files, libs, exes)
  COMMAND ${CMAKE_COMMAND} -E echo "==> CMake clean in: ${CMAKE_BINARY_DIR}"
  COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}" --target clean
  # 2) Cargo clean (Rust target/)
  COMMAND ${CMAKE_COMMAND} -E echo "==> cargo clean in: ${RUST_ROOT}"
  COMMAND ${CMAKE_COMMAND} -E chdir "${RUST_ROOT}" "${CARGO_EXECUTABLE}" clean
  # 3) Remove generated protobufs (lives under build/)
  COMMAND ${CMAKE_COMMAND} -E echo "==> removing generated protobufs: ${PROTO_BINARY_DIR}"
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${PROTO_BINARY_DIR}"
  # 4) Remove the entire build directory (like `rm -rf build`)
  #    Switch to SOURCE dir first so removing BINARY dir is always safe.
  COMMAND ${CMAKE_COMMAND} -E echo "==> removing build directory: ${CMAKE_BINARY_DIR}"
  COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_SOURCE_DIR}" ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}"
  COMMENT "Full clean: CMake outputs + Rust target + generated protos + delete build/"
  VERBATIM
)