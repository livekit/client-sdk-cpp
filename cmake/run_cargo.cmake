# Copyright 2026 LiveKit, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

if(NOT DEFINED CFG)
  set(CFG Debug)
endif()
if(NOT DEFINED RUST_ROOT)
  message(FATAL_ERROR "RUST_ROOT not set")
endif()
if(NOT DEFINED CARGO)
  message(FATAL_ERROR "CARGO not set")
endif()

set(ARGS build --package livekit-ffi)
if(NOT CFG STREQUAL "Debug")
  list(APPEND ARGS --release)
endif()

if(CARGO_NO_DEFAULT_FEATURES)
  list(APPEND ARGS --no-default-features)
endif()

if(DEFINED CARGO_FEATURES AND NOT CARGO_FEATURES STREQUAL "")
  list(APPEND ARGS --features "${CARGO_FEATURES}")
  message(STATUS "[run_cargo.cmake] Cargo features: ${CARGO_FEATURES}")
endif()

if(DEFINED RUST_TARGET AND NOT RUST_TARGET STREQUAL "")
  list(APPEND ARGS --target "${RUST_TARGET}")
  message(STATUS "[run_cargo.cmake] Cross-compiling for target: ${RUST_TARGET}")
endif()

message(STATUS "[run_cargo.cmake] CFG=${CFG}  CARGO=${CARGO}  PROTOC=${PROTOC_PATH}")
set(ENV{PROTOC} "${PROTOC_PATH}")

if(DEFINED CARGO_TARGET_DIR AND NOT CARGO_TARGET_DIR STREQUAL "")
  set(ENV{CARGO_TARGET_DIR} "${CARGO_TARGET_DIR}")
endif()

if(DEFINED GCC_LIB_DIR AND NOT GCC_LIB_DIR STREQUAL "")
  set(ENV{RUSTFLAGS} "-L ${GCC_LIB_DIR} $ENV{RUSTFLAGS}")
  set(ENV{LD_LIBRARY_PATH} "${GCC_LIB_DIR}:$ENV{LD_LIBRARY_PATH}")
endif()

# Point cargo, the cc crate and bindgen at the NDK's toolchain for the target, as cargo-ndk does. webrtc-sys
# finds the NDK through ANDROID_NDK_HOME.
if(DEFINED ANDROID_NDK AND NOT ANDROID_NDK STREQUAL "")
  string(REPLACE "-" "_" _target_env "${RUST_TARGET}")
  string(TOUPPER "${_target_env}" _target_env_upper)
  get_filename_component(_toolchain_bin "${ANDROID_CC}" DIRECTORY)
  get_filename_component(_toolchain_root "${_toolchain_bin}" DIRECTORY)
  set(ENV{ANDROID_NDK_HOME} "${ANDROID_NDK}")
  set(ENV{CC_${_target_env}} "${ANDROID_CC}")
  set(ENV{CXX_${_target_env}} "${ANDROID_CC}++")
  set(ENV{AR_${_target_env}} "${ANDROID_AR}")
  set(ENV{CARGO_TARGET_${_target_env_upper}_LINKER} "${ANDROID_CC}")
  set(ENV{BINDGEN_EXTRA_CLANG_ARGS_${_target_env}} "--sysroot=${_toolchain_root}/sysroot")
  message(STATUS "[run_cargo.cmake] Android NDK=${ANDROID_NDK}  CC=${ANDROID_CC}")
endif()

execute_process(
  COMMAND "${CARGO}" ${ARGS}
  WORKING_DIRECTORY "${RUST_ROOT}"
  RESULT_VARIABLE rv
)
if(rv)
  message(FATAL_ERROR "cargo build failed with code: ${rv}")
endif()
