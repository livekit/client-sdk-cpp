#!/usr/bin/env bash

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

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  echo "ERROR: This script must be sourced so it can export the compiler environment:" >&2
  echo "  source scripts/setup-linux-toolchain.sh" >&2
  exit 1
fi

livekit_compiler_clang_major() {
  local compiler="$1"
  "${compiler}" -dM -E -x c /dev/null 2>/dev/null \
    | awk '$2 == "__clang_major__" { print $3; exit }'
}

livekit_resolve_compiler() {
  local compiler="$1"
  if [[ "${compiler}" == */* ]]; then
    [[ -x "${compiler}" ]] || return 1
    printf '%s\n' "${compiler}"
  else
    command -v "${compiler}" 2>/dev/null
  fi
}

livekit_find_libclang_dir() {
  local llvm_root="$1"
  local clang_major="$2"
  local -a search_dirs=(
    "${llvm_root}/lib"
    "${llvm_root}/lib64"
    "/usr/lib/llvm-${clang_major}/lib"
  )
  local directory candidate

  for directory in "${search_dirs[@]}"; do
    [[ -d "${directory}" ]] || continue
    for candidate in \
      "${directory}/libclang.so" \
      "${directory}"/libclang.so.* \
      "${directory}"/libclang-*.so \
      "${directory}"/libclang-*.so.*; do
      [[ -f "${candidate}" ]] || continue
      [[ "$(basename "${candidate}")" == *-cpp.* ]] && continue
      printf '%s\n' "${directory}"
      return 0
    done
  done

  return 1
}

livekit_select_linux_clang_pair() {
  local candidate_cc="$1"
  local candidate_cxx="$2"
  local resolved_cc resolved_cxx cc_major cxx_major llvm_root libclang_dir

  resolved_cc="$(livekit_resolve_compiler "${candidate_cc}")" || return 1
  resolved_cxx="$(livekit_resolve_compiler "${candidate_cxx}")" || return 1
  cc_major="$(livekit_compiler_clang_major "${resolved_cc}")"
  cxx_major="$(livekit_compiler_clang_major "${resolved_cxx}")"

  [[ -n "${cc_major}" && "${cc_major}" == "${cxx_major}" ]] || return 1
  ((cc_major >= 21)) || return 1

  llvm_root="$(cd "$(dirname "${resolved_cxx}")/.." && pwd -P)"
  libclang_dir="$(livekit_find_libclang_dir "${llvm_root}" "${cxx_major}")" || return 1

  CC="${resolved_cc}"
  CXX="${resolved_cxx}"
  LIBCLANG_PATH="${libclang_dir}"
  export CC CXX LIBCLANG_PATH
  return 0
}

livekit_configure_linux_toolchain() {
  local -a llvm_roots=()
  local root version

  if [[ -n "${LLVM_ROOT:-}" ]]; then
    if ! livekit_select_linux_clang_pair "${LLVM_ROOT}/bin/clang" "${LLVM_ROOT}/bin/clang++"; then
      echo "ERROR: LLVM_ROOT=${LLVM_ROOT} does not contain clang/clang++ 21+ and libclang." >&2
      return 1
    fi
  elif [[ -n "${CC:-}" || -n "${CXX:-}" ]]; then
    if [[ -z "${CC:-}" || -z "${CXX:-}" ]]; then
      echo "ERROR: CC and CXX must both be set when selecting a compiler explicitly." >&2
      return 1
    fi
    if ! livekit_select_linux_clang_pair "${CC}" "${CXX}"; then
      echo "ERROR: CC=${CC} and CXX=${CXX} must select matching clang 21+ compilers with libclang." >&2
      return 1
    fi
  else
    shopt -s nullglob
    llvm_roots=(/opt/llvm-*)
    shopt -u nullglob
    if ((${#llvm_roots[@]})); then
      while IFS= read -r root; do
        if livekit_select_linux_clang_pair "${root}/bin/clang" "${root}/bin/clang++"; then
          break
        fi
      done < <(printf '%s\n' "${llvm_roots[@]}" | sort -Vr)
    fi

    if [[ -z "${CC:-}" ]]; then
      for ((version = 40; version >= 21; --version)); do
        if livekit_select_linux_clang_pair "clang-${version}" "clang++-${version}"; then
          break
        fi
      done
    fi

    if [[ -z "${CC:-}" ]] && ! livekit_select_linux_clang_pair "clang" "clang++"; then
      echo "ERROR: Linux builds require clang/clang++ 21+ and libclang." >&2
      echo "Ubuntu 22.04 and 24.04 distro clang versions are too old." >&2
      echo "Install clang-21 from apt.llvm.org or set LLVM_ROOT to a compatible LLVM installation." >&2
      return 1
    fi
  fi

  echo "==> Linux compiler: $("${CXX}" --version | awk 'NR == 1 { print; exit }')"
  echo "==> CC=${CC}"
  echo "==> CXX=${CXX}"
  echo "==> LIBCLANG_PATH=${LIBCLANG_PATH}"
}

if livekit_configure_linux_toolchain; then
  livekit_setup_status=0
else
  livekit_setup_status=$?
fi

unset -f livekit_compiler_clang_major
unset -f livekit_resolve_compiler
unset -f livekit_find_libclang_dir
unset -f livekit_select_linux_clang_pair
unset -f livekit_configure_linux_toolchain

if ((livekit_setup_status != 0)); then
  unset livekit_setup_status
  return 1
fi
unset livekit_setup_status
