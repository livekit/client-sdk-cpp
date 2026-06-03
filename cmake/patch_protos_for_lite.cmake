# cmake/patch_protos_for_lite.cmake
#
# Copy upstream .proto files (from client-sdk-rust/livekit-ffi/protocol/) into
# a build-tree staging directory and append `option optimize_for = LITE_RUNTIME;`
# to each one. The C++ SDK then compiles the patched copies, which causes
# protoc to emit code linking only against libprotobuf-lite (no reflection,
# no descriptors, no text-format, no JSON).
#
# This patches *only* the copies used by the C++ build. The original .proto
# files in the client-sdk-rust submodule are untouched, so prost (Rust) and
# protobuf-es (Node) generators continue to see the unmodified sources.
#
# Usage (invoked via `cmake -P`):
#   cmake -DIN_DIR=<dir> -DOUT_DIR=<dir> "-DFILES=a.proto;b.proto;..." \
#         -P patch_protos_for_lite.cmake

if(NOT DEFINED IN_DIR OR IN_DIR STREQUAL "")
  message(FATAL_ERROR "patch_protos_for_lite.cmake: IN_DIR is required")
endif()
if(NOT DEFINED OUT_DIR OR OUT_DIR STREQUAL "")
  message(FATAL_ERROR "patch_protos_for_lite.cmake: OUT_DIR is required")
endif()
if(NOT DEFINED FILES OR FILES STREQUAL "")
  message(FATAL_ERROR "patch_protos_for_lite.cmake: FILES is required")
endif()

file(MAKE_DIRECTORY "${OUT_DIR}")

set(_marker "// --- appended by client-sdk-cpp: force lite runtime for C++ codegen ---")
set(_option_line "option optimize_for = LITE_RUNTIME;")

foreach(_name IN LISTS FILES)
  set(_src "${IN_DIR}/${_name}")
  set(_dst "${OUT_DIR}/${_name}")

  if(NOT EXISTS "${_src}")
    message(FATAL_ERROR "patch_protos_for_lite.cmake: missing source ${_src}")
  endif()

  file(READ "${_src}" _content)

  # If upstream already opts into lite, keep the file unchanged. If it chooses
  # another runtime, fail rather than silently overriding that choice.
  string(REGEX MATCH "(^|\n)[ \t]*option[ \t]+optimize_for[ \t]*=[ \t]*LITE_RUNTIME[ \t]*;" _existing_lite "${_content}")
  if(_existing_lite)
    file(WRITE "${_dst}" "${_content}")
    continue()
  endif()

  string(REGEX MATCH "(^|\n)[ \t]*option[ \t]+optimize_for[ \t]*=" _existing_runtime "${_content}")
  if(_existing_runtime)
    message(FATAL_ERROR
      "patch_protos_for_lite.cmake: ${_name} declares a non-lite optimize_for. "
      "The LiveKit C++ SDK requires LITE_RUNTIME.")
  endif()

  # Append the option as a trailing line. File-level options have no ordering
  # requirement in protobuf grammar, so this is always safe.
  file(WRITE "${_dst}"
       "${_content}\n${_marker}\n${_option_line}\n")
endforeach()
