#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
LOCAL_INSTALL_DIR="${PROJECT_ROOT}/local-install"
BUILD_TYPE="Release"
PRESET=""

# Initialize optional variables (required for set -u)
DO_BUNDLE="0"
DO_ARCHIVE="0"
PREFIX=""
ARCHIVE_NAME=""
GENERATOR=""
MACOS_ARCH=""
LIVEKIT_VERSION=""
CAPTURE_OVERRIDE="none"

# Detect OS for preset selection
detect_os() {
  case "$(uname -s)" in
    Linux*)     echo "linux";;
    Darwin*)    echo "macos";;
    *)          echo "linux";;  # Default to linux
  esac
}

OS_TYPE="$(detect_os)"

compiler_clang_major() {
  local compiler="$1"
  "${compiler}" -dM -E -x c /dev/null 2>/dev/null \
    | awk '$2 == "__clang_major__" { print $3; exit }'
}

resolve_compiler() {
  local compiler="$1"
  if [[ "${compiler}" == */* ]]; then
    [[ -x "${compiler}" ]] || return 1
    printf '%s\n' "${compiler}"
  else
    command -v "${compiler}" 2>/dev/null
  fi
}

find_libclang_dir() {
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

select_linux_clang_pair() {
  local candidate_cc="$1"
  local candidate_cxx="$2"
  local resolved_cc resolved_cxx cc_major cxx_major llvm_root libclang_dir

  resolved_cc="$(resolve_compiler "${candidate_cc}")" || return 1
  resolved_cxx="$(resolve_compiler "${candidate_cxx}")" || return 1
  cc_major="$(compiler_clang_major "${resolved_cc}")"
  cxx_major="$(compiler_clang_major "${resolved_cxx}")"

  [[ -n "${cc_major}" && "${cc_major}" == "${cxx_major}" ]] || return 1
  ((cc_major >= 21)) || return 1

  llvm_root="$(cd "$(dirname "${resolved_cxx}")/.." && pwd -P)"
  libclang_dir="$(find_libclang_dir "${llvm_root}" "${cxx_major}")" || return 1

  CC="${resolved_cc}"
  CXX="${resolved_cxx}"
  LIBCLANG_PATH="${libclang_dir}"
  export CC CXX LIBCLANG_PATH
  return 0
}

configure_linux_toolchain() {
  local -a llvm_roots=()
  local root version

  if [[ -n "${LLVM_ROOT:-}" ]]; then
    if ! select_linux_clang_pair "${LLVM_ROOT}/bin/clang" "${LLVM_ROOT}/bin/clang++"; then
      echo "ERROR: LLVM_ROOT=${LLVM_ROOT} does not contain clang/clang++ 21+ and libclang." >&2
      exit 1
    fi
  elif [[ -n "${CC:-}" || -n "${CXX:-}" ]]; then
    if [[ -z "${CC:-}" || -z "${CXX:-}" ]]; then
      echo "ERROR: CC and CXX must both be set when selecting a compiler explicitly." >&2
      exit 1
    fi
    if ! select_linux_clang_pair "${CC}" "${CXX}"; then
      echo "ERROR: CC=${CC} and CXX=${CXX} must select matching clang 21+ compilers with libclang." >&2
      exit 1
    fi
  else
    shopt -s nullglob
    llvm_roots=(/opt/llvm-*)
    shopt -u nullglob
    if ((${#llvm_roots[@]})); then
      while IFS= read -r root; do
        if select_linux_clang_pair "${root}/bin/clang" "${root}/bin/clang++"; then
          break
        fi
      done < <(printf '%s\n' "${llvm_roots[@]}" | sort -Vr)
    fi

    if [[ -z "${CC:-}" ]]; then
      for ((version = 40; version >= 21; --version)); do
        if select_linux_clang_pair "clang-${version}" "clang++-${version}"; then
          break
        fi
      done
    fi

    if [[ -z "${CC:-}" ]] && ! select_linux_clang_pair "clang" "clang++"; then
      echo "ERROR: Linux builds require clang/clang++ 21+ and libclang." >&2
      echo "Ubuntu 22.04 and 24.04 distro clang versions are too old." >&2
      echo "Install clang-21 from apt.llvm.org or set LLVM_ROOT to a compatible LLVM installation." >&2
      exit 1
    fi
  fi

  echo "==> Linux compiler: $("${CXX}" --version | awk 'NR == 1 { print; exit }')"
  echo "==> CC=${CC}"
  echo "==> CXX=${CXX}"
  echo "==> LIBCLANG_PATH=${LIBCLANG_PATH}"
}

configure_macos_toolchain() {
  if ! command -v xcrun >/dev/null 2>&1; then
    echo "ERROR: xcrun is unavailable. Install Xcode Command Line Tools." >&2
    exit 1
  fi

  CC="$(xcrun --find clang)"
  CXX="$(xcrun --find clang++)"
  export CC CXX

  echo "==> macOS compiler: $("${CXX}" --version | awk 'NR == 1 { print; exit }')"
  echo "==> CC=${CC}"
  echo "==> CXX=${CXX}"
}

configure_build_toolchain() {
  case "${OS_TYPE}" in
    linux) configure_linux_toolchain ;;
    macos) configure_macos_toolchain ;;
  esac
}

usage() {
  cat <<EOF
Usage:
  ./build.sh <command> [options]

Commands:
  debug             Configure + build Debug version (build-debug/)
  debug-examples    Configure + build Debug version with examples
  debug-tests       Configure + build Debug version with tests
  debug-all         Configure + build Debug version with tests + examples
  release           Configure + build Release version (build-release/)
  release-examples  Configure + build Release version with examples
  release-tests     Configure + build Release version with tests
  release-all       Configure + build Release version with tests + examples
  build-all         Configure + build all of the above (debug/release + examples + tests)
  clean             Clean both Debug and Release build directories + local-install
  clean-all         Full clean (build dirs + local-install + Rust targets)
  help              Show this help message

Options (for debug / release):
  --capture[=list]         Enable capture sources (default: all).
                           list: gstreamer, pattern, clock (comma-separated)
  --no-capture             Explicitly disable capture sources (the default)
  --bundle                 Install the SDK bundle using 'cmake --install'
  --prefix <dir>           Install prefix for --bundle
                           (default: ./sdk-out/livekit-sdk)
  --archive                After --bundle, create an archive of the SDK bundle
                           (.zip if available, otherwise .tar.gz)
  --archive-name <name>    Override archive base name (no extension)
  --version <version>      Inject SDK version into build (sets LIVEKIT_VERSION)
                           Example: 0.1.0 or 1.0.0-rc1
  -G <generator>           CMake generator (e.g. Ninja, "Unix Makefiles")
  --macos-arch <arch>      macOS architecture override (arm64 or x86_64)
                           Sets CMAKE_OSX_ARCHITECTURES

Examples:
  ./build.sh release
  ./build.sh release-examples
  ./build.sh debug
  ./build.sh debug-tests
  ./build.sh release-tests
  ./build.sh build-all
  ./build.sh clean
  ./build.sh clean-all
EOF
}


# Capture is none, all, or a comma-separated list of gstreamer, pattern, and clock.
validate_capture_value() {
  local lower token
  lower="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | tr -d '[:space:]')"
  case "${lower}" in
    ""|none|all) return 0 ;;
  esac
  local IFS=','
  for token in ${lower}; do
    case "${token}" in
      ""|gstreamer|pattern|clock) ;;
      *)
        echo "ERROR: Unknown capture source '${token}'. Expected: gstreamer, pattern, clock, all, or none."
        exit 1
        ;;
    esac
  done
}

parse_opts() {
  shift || true
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --capture)
        CAPTURE_OVERRIDE="all"
        shift
        ;;
      --capture=*)
        CAPTURE_OVERRIDE="${1#*=}"
        if [[ -z "${CAPTURE_OVERRIDE}" ]]; then
          echo "ERROR: --capture= requires a value (gstreamer, pattern, clock, or all)"
          exit 1
        fi
        shift
        ;;
      --no-capture)
        CAPTURE_OVERRIDE="none"
        shift
        ;;
      --bundle)
        DO_BUNDLE="1"
        shift
        ;;
      --prefix)
        PREFIX="${2:-}"
        if [[ -z "${PREFIX}" ]]; then
          echo "ERROR: --prefix requires a value"
          exit 1
        fi
        shift 2
        ;;
      --archive)
        DO_ARCHIVE="1"
        shift
        ;;
      --archive-name)
        ARCHIVE_NAME="${2:-}"
        if [[ -z "${ARCHIVE_NAME}" ]]; then
          echo "ERROR: --archive-name requires a value"
          exit 1
        fi
        shift 2
        ;;
      -G)
        GENERATOR="${2:-}"
        if [[ -z "${GENERATOR}" ]]; then
          echo "ERROR: -G requires a generator name"
          exit 1
        fi
        shift 2
        ;;
      --macos-arch)
        MACOS_ARCH="${2:-}"
        if [[ -z "${MACOS_ARCH}" ]]; then
          echo "ERROR: --macos-arch requires a value (arm64 or x86_64)"
          exit 1
        fi
        shift 2
        ;;
      --version)
        LIVEKIT_VERSION="${2:-}"
        [[ -n "${LIVEKIT_VERSION}" ]] || { echo "ERROR: --version requires a value"; exit 1; }
        shift 2
        ;;
      -h|--help|help)
        usage
        exit 0
        ;;
      *)
        echo "ERROR: Unknown option: $1"
        usage
        exit 1
        ;;
    esac
  done
  validate_capture_value "${CAPTURE_OVERRIDE}"
}

configure() {
  if [[ "${OS_TYPE}" == "linux" && -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    local cached_cc cached_cxx selected_cc selected_cxx
    cached_cc="$(awk -F= '$1 == "CMAKE_C_COMPILER:FILEPATH" { print $2; exit }' \
      "${BUILD_DIR}/CMakeCache.txt")"
    cached_cxx="$(awk -F= '$1 == "CMAKE_CXX_COMPILER:FILEPATH" { print $2; exit }' \
      "${BUILD_DIR}/CMakeCache.txt")"
    selected_cc="$(readlink -f "${CC}")"
    selected_cxx="$(readlink -f "${CXX}")"
    if [[ -n "${cached_cc}" && "$(readlink -f "${cached_cc}")" != "${selected_cc}" ]] \
      || [[ -n "${cached_cxx}" && "$(readlink -f "${cached_cxx}")" != "${selected_cxx}" ]]; then
      echo "ERROR: ${BUILD_DIR} was configured with a different compiler:" >&2
      echo "  cached CC=${cached_cc}" >&2
      echo "  cached CXX=${cached_cxx}" >&2
      echo "  selected CC=${CC}" >&2
      echo "  selected CXX=${CXX}" >&2
      echo "Run './build.sh clean' and retry." >&2
      exit 1
    fi
  fi

  echo "==> Configuring CMake (${BUILD_TYPE}) using preset ${PRESET}..."
  local -a extra_args=("-DLIVEKIT_CAPTURE=${CAPTURE_OVERRIDE}")
  if [[ -n "${LIVEKIT_VERSION}" ]]; then
    echo "==> Injecting LIVEKIT_VERSION=${LIVEKIT_VERSION}"
    extra_args+=("-DLIVEKIT_VERSION=${LIVEKIT_VERSION}")
  fi
  if [[ -n "${MACOS_ARCH}" ]]; then
    echo "==> Setting CMAKE_OSX_ARCHITECTURES=${MACOS_ARCH}"
    extra_args+=("-DCMAKE_OSX_ARCHITECTURES=${MACOS_ARCH}")
  fi
  if ((${#extra_args[@]})); then
    if ! cmake --preset "${PRESET}" "${extra_args[@]}"; then
      echo "Warning: CMake preset '${PRESET}' failed. Falling back to traditional configure..."
      cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" "${extra_args[@]}"
    fi
  else
    if ! cmake --preset "${PRESET}"; then
      echo "Warning: CMake preset '${PRESET}' failed. Falling back to traditional configure..."
      cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    fi
  fi

  if [[ -f "${BUILD_DIR}/compile_commands.json" ]]; then
    ln -sf "${BUILD_DIR}/compile_commands.json" "${PROJECT_ROOT}/compile_commands.json"
    echo "==> Symlinked compile_commands.json -> ${BUILD_DIR}/compile_commands.json"
  fi
}

detect_parallel_jobs() {
  if [[ -n "${CMAKE_BUILD_PARALLEL_LEVEL:-}" ]]; then
    echo "${CMAKE_BUILD_PARALLEL_LEVEL}"
    return
  fi

  local jobs=""
  if command -v getconf >/dev/null 2>&1; then
    jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
  fi
  if [[ -z "${jobs}" || ! "${jobs}" =~ ^[0-9]+$ || "${jobs}" -lt 1 ]]; then
    if command -v sysctl >/dev/null 2>&1; then
      jobs="$(sysctl -n hw.ncpu 2>/dev/null || true)"
    fi
  fi
  if [[ -z "${jobs}" || ! "${jobs}" =~ ^[0-9]+$ || "${jobs}" -lt 1 ]]; then
    jobs="2"
  fi

  echo "${jobs}"
}

build() {
  local parallel_jobs
  parallel_jobs="$(detect_parallel_jobs)"

  echo "==> Building (${BUILD_TYPE}) with ${parallel_jobs} parallel jobs..."
  if [[ -n "${PRESET}" ]] && [[ -f "${PROJECT_ROOT}/CMakePresets.json" ]]; then
    # Use preset build if available
    cmake --build --preset "${PRESET}" --parallel "${parallel_jobs}"
  else
    # Fallback to traditional build
    cmake --build "${BUILD_DIR}" --parallel "${parallel_jobs}"
  fi
}

install_bundle() {
  # Default prefix if user asked for --bundle but didn't set one
  if [[ -z "${PREFIX}" ]]; then
    PREFIX="${PROJECT_ROOT}/sdk-out/livekit-sdk"
  fi

  # Make prefix absolute for nicer archives
  if [[ "${PREFIX}" != /* ]]; then
    PREFIX="${PROJECT_ROOT}/${PREFIX}"
  fi

  echo "==> Installing SDK bundle to: ${PREFIX}"
  rm -rf "${PREFIX}"
  mkdir -p "${PREFIX}"

  # Detect whether generator is multi-config (VS/Xcode) or single-config (Ninja/Unix Makefiles)
  local is_multi_config=0
  if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    if grep -q '^CMAKE_CONFIGURATION_TYPES:STRING=' "${BUILD_DIR}/CMakeCache.txt"; then
      is_multi_config=1
    fi
  fi

  # Run install
  if [[ "${is_multi_config}" -eq 1 ]]; then
    echo "==> cmake --install (multi-config) config=${BUILD_TYPE}"
    cmake --install "${BUILD_DIR}" --config "${BUILD_TYPE}" --prefix "${PREFIX}"
  else
    echo "==> cmake --install (single-config)"
    cmake --install "${BUILD_DIR}" --prefix "${PREFIX}"
  fi

  local libdir=""
  if [[ -d "${PREFIX}/lib" ]]; then
    libdir="${PREFIX}/lib"
  elif [[ -d "${PREFIX}/lib64" ]]; then
    libdir="${PREFIX}/lib64"
  else
    echo "WARN: ${PREFIX}/lib or lib64 not found. Did you add install(TARGETS ...) rules?"
  fi

  if [[ -n "${libdir}" ]]; then
    if [[ ! -d "${libdir}/cmake/LiveKit" ]]; then
      echo "WARN: CMake package files not found under ${libdir}/cmake/LiveKit."
      echo "      Did you add install(EXPORT ...) + install(FILES LiveKitConfig*.cmake ...)?"
    else
      # Optional: verify that key files exist
      if [[ ! -f "${libdir}/cmake/LiveKit/LiveKitConfig.cmake" ]]; then
        echo "WARN: Missing ${libdir}/cmake/LiveKit/LiveKitConfig.cmake"
      fi
      if [[ ! -f "${libdir}/cmake/LiveKit/LiveKitTargets.cmake" ]]; then
        echo "WARN: Missing ${libdir}/cmake/LiveKit/LiveKitTargets.cmake (install(EXPORT ...) didn’t run?)"
      fi
      if [[ ! -f "${libdir}/cmake/LiveKit/LiveKitConfigVersion.cmake" ]]; then
        echo "WARN: Missing ${libdir}/cmake/LiveKit/LiveKitConfigVersion.cmake"
      fi
    fi
  fi
}


archive_bundle() {
  if [[ "${DO_BUNDLE}" != "1" ]]; then
    echo "ERROR: --archive requires --bundle"
    exit 1
  fi

  local base
  if [[ -n "${ARCHIVE_NAME}" ]]; then
    base="${ARCHIVE_NAME}"
  else
    base="$(basename "${PREFIX}")"
  fi

  local out_dir
  out_dir="$(dirname "${PREFIX}")"

  echo "==> Archiving bundle..."
  pushd "${out_dir}" >/dev/null

  # Prefer zip if available and user is on macOS/Linux too; otherwise tar.gz
  if command -v zip >/dev/null 2>&1; then
    rm -f "${base}.zip"
    # Zip the directory (preserve folder root)
    zip -r "${base}.zip" "${base}" >/dev/null
    echo "==> Wrote: ${out_dir}/${base}.zip"
  else
    rm -f "${base}.tar.gz"
    tar -czf "${base}.tar.gz" "${base}"
    echo "==> Wrote: ${out_dir}/${base}.tar.gz"
  fi

  popd >/dev/null
}

clean() {
  echo "==> Cleaning build artifacts..."
  local debug_dir="${PROJECT_ROOT}/build-debug"
  local release_dir="${PROJECT_ROOT}/build-release"
  
  # For Ninja builds, use ninja -t clean directly to avoid CMake reconfiguration
  # For other generators (e.g., Make), cmake --build --target clean works fine
  
  if [[ -d "${debug_dir}" ]]; then
    echo "   Cleaning build-debug..."
    if [[ -f "${debug_dir}/build.ninja" ]]; then
      # Ninja: use -t clean to avoid reconfiguration
      ninja -C "${debug_dir}" -t clean 2>/dev/null || rm -rf "${debug_dir}/lib" "${debug_dir}/bin" || true
    elif [[ -f "${debug_dir}/Makefile" ]]; then
      make -C "${debug_dir}" clean 2>/dev/null || true
    else
      echo "      (skipping) Unknown build system or not configured"
    fi
    rm -f "${debug_dir}/CMakeCache.txt"
    rm -rf "${debug_dir}/CMakeFiles"
  else
    echo "   (skipping) build-debug does not exist."
  fi
  
  if [[ -d "${release_dir}" ]]; then
    echo "   Cleaning build-release..."
    if [[ -f "${release_dir}/build.ninja" ]]; then
      # Ninja: use -t clean to avoid reconfiguration
      ninja -C "${release_dir}" -t clean 2>/dev/null || rm -rf "${release_dir}/lib" "${release_dir}/bin" || true
    elif [[ -f "${release_dir}/Makefile" ]]; then
      make -C "${release_dir}" clean 2>/dev/null || true
    else
      echo "      (skipping) Unknown build system or not configured"
    fi
    rm -f "${release_dir}/CMakeCache.txt"
    rm -rf "${release_dir}/CMakeFiles"
  else
    echo "   (skipping) build-release does not exist."
  fi

  echo "   Removing local-install..."
  rm -rf "${LOCAL_INSTALL_DIR}" || true
  
  echo "==> Clean complete."
}

clean_all() {
  echo "==> Running full clean-all (C++ + local-install + Rust)..."

  echo "Removing build-debug directory..."
  rm -rf "${PROJECT_ROOT}/build-debug" || true

  echo "Removing build-release directory..."
  rm -rf "${PROJECT_ROOT}/build-release" || true

  echo "Removing local-install directory..."
  rm -rf "${LOCAL_INSTALL_DIR}" || true

  echo "Removing Rust target directory..."
  rm -rf "${PROJECT_ROOT}/client-sdk-rust/target" || true

  echo "==> Clean-all complete."
}

if [[ $# -eq 0 ]]; then
  usage
  exit 0
fi

cmd="$1"
parse_opts "$@"
case "${cmd}" in
  debug|debug-examples|debug-tests|debug-all|release|release-examples|release-tests|release-all|build-all)
    configure_build_toolchain
    ;;
esac
case "${cmd}" in
  debug)
    BUILD_TYPE="Debug"
    BUILD_DIR="${PROJECT_ROOT}/build-debug"
    PRESET="${OS_TYPE}-debug"
    configure
    build
    if [[ "${DO_BUNDLE}" == "1" ]]; then
      install_bundle
      if [[ "${DO_ARCHIVE}" == "1" ]]; then
        archive_bundle
      fi
    fi
    ;;
  debug-examples)
    BUILD_TYPE="Debug"
    BUILD_DIR="${PROJECT_ROOT}/build-debug"
    PRESET="${OS_TYPE}-debug-examples"
    configure
    build
    if [[ "${DO_BUNDLE}" == "1" ]]; then
      install_bundle
      if [[ "${DO_ARCHIVE}" == "1" ]]; then
        archive_bundle
      fi
    fi
    ;;
  release)
    BUILD_TYPE="Release"
    BUILD_DIR="${PROJECT_ROOT}/build-release"
    PRESET="${OS_TYPE}-release"
    configure
    build
    if [[ "${DO_BUNDLE}" == "1" ]]; then
      install_bundle
      if [[ "${DO_ARCHIVE}" == "1" ]]; then
        archive_bundle
      fi
    fi
    ;;
  release-examples)
    BUILD_TYPE="Release"
    BUILD_DIR="${PROJECT_ROOT}/build-release"
    PRESET="${OS_TYPE}-release-examples"
    configure
    build
    if [[ "${DO_BUNDLE}" == "1" ]]; then
      install_bundle
      if [[ "${DO_ARCHIVE}" == "1" ]]; then
        archive_bundle
      fi
    fi
    ;;
  debug-tests)
    BUILD_TYPE="Debug"
    BUILD_DIR="${PROJECT_ROOT}/build-debug"
    PRESET="${OS_TYPE}-debug-tests"
    configure
    build
    if [[ "${DO_BUNDLE}" == "1" ]]; then
      install_bundle
      if [[ "${DO_ARCHIVE}" == "1" ]]; then
        archive_bundle
      fi
    fi
    ;;
  release-tests)
    BUILD_TYPE="Release"
    BUILD_DIR="${PROJECT_ROOT}/build-release"
    PRESET="${OS_TYPE}-release-tests"
    configure
    build
    if [[ "${DO_BUNDLE}" == "1" ]]; then
      install_bundle
      if [[ "${DO_ARCHIVE}" == "1" ]]; then
        archive_bundle
      fi
    fi
    ;;
  release-all)
    BUILD_TYPE="Release"
    BUILD_DIR="${PROJECT_ROOT}/build-release"
    PRESET="${OS_TYPE}-release-all"
    configure
    build
    if [[ "${DO_BUNDLE}" == "1" ]]; then
      install_bundle
      if [[ "${DO_ARCHIVE}" == "1" ]]; then
        archive_bundle
      fi
    fi
    ;;
  debug-all)
    BUILD_TYPE="Debug"
    BUILD_DIR="${PROJECT_ROOT}/build-debug"
    PRESET="${OS_TYPE}-debug-all"
    configure
    build
    if [[ "${DO_BUNDLE}" == "1" ]]; then
      install_bundle
      if [[ "${DO_ARCHIVE}" == "1" ]]; then
        archive_bundle
      fi
    fi
    ;;
  build-all)
    echo "==> Build-all: debug, debug-examples, debug-tests, release, release-examples, release-tests"
    BUILD_TYPE="Debug"
    BUILD_DIR="${PROJECT_ROOT}/build-debug"
    PRESET="${OS_TYPE}-debug"
    configure && build
    PRESET="${OS_TYPE}-debug-examples"
    configure && build
    PRESET="${OS_TYPE}-debug-tests"
    configure && build
    BUILD_TYPE="Release"
    BUILD_DIR="${PROJECT_ROOT}/build-release"
    PRESET="${OS_TYPE}-release"
    configure && build
    PRESET="${OS_TYPE}-release-examples"
    configure && build
    PRESET="${OS_TYPE}-release-tests"
    configure && build
    echo "==> Build-all complete."
    ;;
  clean)
    clean
    ;;
  clean-all)
    clean_all
    ;;
  help|-h|--help)
    usage
    ;;
  *)
    echo "Unknown command: ${cmd}"
    usage
    exit 1
    ;;
esac
