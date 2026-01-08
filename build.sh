#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
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

# Detect OS for preset selection
detect_os() {
  case "$(uname -s)" in
    Linux*)     echo "linux";;
    Darwin*)    echo "macos";;
    *)          echo "linux";;  # Default to linux
  esac
}

OS_TYPE="$(detect_os)"

usage() {
  cat <<EOF
Usage:
  ./build.sh <command> [options]

Commands:
  debug             Configure + build Debug version (build-debug/)
  debug-examples    Configure + build Debug version with examples
  release           Configure + build Release version (build-release/)
  release-examples  Configure + build Release version with examples
  clean             Clean both Debug and Release build directories
  clean-all         Full clean (build dirs + Rust targets)
  help              Show this help message

Options (for debug / release):
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
  ./build.sh clean
  ./build.sh clean-all
EOF
}


parse_opts() {
  shift || true
  while [[ $# -gt 0 ]]; do
    case "$1" in
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
}

configure() {
  echo "==> Configuring CMake (${BUILD_TYPE}) using preset ${PRESET}..."
  if ! cmake --preset "${PRESET}"; then
    echo "Warning: CMake preset '${PRESET}' failed. Falling back to traditional configure..."
    cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
  fi
}

build() {
  echo "==> Building (${BUILD_TYPE})..."
  if [[ -n "${PRESET}" ]] && [[ -f "${PROJECT_ROOT}/CMakePresets.json" ]]; then
    # Use preset build if available
    cmake --build --preset "${PRESET}"
  else
    # Fallback to traditional build
    cmake --build "${BUILD_DIR}"
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
  else
    echo "   (skipping) build-release does not exist."
  fi
  
  echo "==> Clean complete."
}

clean_all() {
  echo "==> Running full clean-all (C++ + Rust)..."
  
  echo "Removing build-debug directory..."
  rm -rf "${PROJECT_ROOT}/build-debug" || true
  
  echo "Removing build-release directory..."
  rm -rf "${PROJECT_ROOT}/build-release" || true
  
  echo "Removing Rust debug artifacts..."
  rm -rf "${PROJECT_ROOT}/client-sdk-rust/target/debug" || true
  
  echo "Removing Rust release artifacts..."
  rm -rf "${PROJECT_ROOT}/client-sdk-rust/target/release" || true
  
  echo "==> Clean-all complete."
}

if [[ $# -eq 0 ]]; then
  usage
  exit 0
fi

cmd="$1"
parse_opts "$@"
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
