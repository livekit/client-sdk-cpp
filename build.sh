#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
BUILD_TYPE="Release"
VERBOSE=""
PRESET=""

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
Usage: ./build.sh [command] [options]

Commands:
  debug             Configure + build Debug version
  release           Configure + build Release version
  clean             Run CMake's built-in clean target
  clean-all         Run clean_all (clears C++ + Rust targets)
  verbose           Build with verbose output (implies last configured type)
  help              Show this help

Examples:
  ./build.sh debug
  ./build.sh release
  ./build.sh clean
  ./build.sh clean-all
  ./build.sh verbose
EOF
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
    cmake --build --preset "${PRESET}" ${VERBOSE:+--verbose}
  else
    # Fallback to traditional build
    cmake --build "${BUILD_DIR}" -j ${VERBOSE:+--verbose}
  fi
}

clean() {
  echo "==> Cleaning CMake targets..."
  if [[ -d "${BUILD_DIR}" ]]; then
    cmake --build "${BUILD_DIR}" --target clean || true
  else
    echo "   (skipping) ${BUILD_DIR} does not exist."
  fi
}

clean_all() {
  echo "==> Running full clean-all (C++ + Rust)..."
  if [[ -d "${BUILD_DIR}" ]]; then
    cmake --build "${BUILD_DIR}" --target clean_all || true
  else
    echo "   (info) ${BUILD_DIR} does not exist; doing manual deep clean..."
  fi

  rm -rf "${PROJECT_ROOT}/client-sdk-rust/target/debug" || true
  rm -rf "${PROJECT_ROOT}/client-sdk-rust/target/release" || true
  rm -rf "${BUILD_DIR}" || true
  echo "==> Clean-all complete."
}


if [[ $# -eq 0 ]]; then
  usage
  exit 0
fi

case "$1" in
  debug)
    BUILD_TYPE="Debug"
    PRESET="${OS_TYPE}-debug"
    configure
    build
    ;;
  release)
    BUILD_TYPE="Release"
    PRESET="${OS_TYPE}-release"
    configure
    build
    ;;
  verbose)
    VERBOSE="1"
    build
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
    echo "Unknown command: $1"
    usage
    exit 1
    ;;
esac

