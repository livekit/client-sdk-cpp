#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
BUILD_TYPE="Release"
VERBOSE=""
GENERATOR=""          # optional, e.g. Ninja
PREFIX=""             # install prefix for bundling
DO_BUNDLE="0"         # whether to run cmake --install
DO_ARCHIVE="0"        # whether to create .tar.gz/.zip
ARCHIVE_NAME=""       # optional override
MACOS_ARCH=""         # optional: arm64 or x86_64 (for macOS only)


usage() {
  cat <<EOF
Usage:
  ./build.sh <command> [options]

Commands:
  debug             Configure + build Debug version
  release           Configure + build Release version
  verbose           Build with verbose output (uses last configured build)
  clean             Run CMake's built-in clean target
  clean-all         Run full clean (C++ build + Rust targets + generated files)
  help              Show this help message

Options (for debug / release / verbose):
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
  ./build.sh release --bundle
  ./build.sh release --bundle --archive
  ./build.sh release --bundle --prefix ./sdk-out/livekit-sdk-macos-arm64
  ./build.sh debug --bundle --prefix /tmp/livekit-sdk-debug
  ./build.sh release --version 0.1.0 --bundle --archive
  ./build.sh release -G Ninja --macos-arch arm64 --bundle \\
      --archive-name livekit-sdk-0.1.0-macos-arm64

Notes:
  - '--bundle' installs a consumable SDK layout containing:
      * headers under include/
      * libraries under lib/ (and bin/ if shared)
      * CMake package files under lib/cmake/LiveKit/
  - '--archive' requires '--bundle'
  - CI builds should use '--version' to ensure build.h matches the release tag
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
  echo "==> Configuring CMake (${BUILD_TYPE})..."

  local cmake_args=(
    -S "${PROJECT_ROOT}"
    -B "${BUILD_DIR}"
  )

  # Generator
  if [[ -n "${GENERATOR}" ]]; then
    cmake_args+=(-G "${GENERATOR}")
  fi

  # Version
  if [[ -n "${LIVEKIT_VERSION:-}" ]]; then
    cmake_args+=(-DLIVEKIT_VERSION="${LIVEKIT_VERSION}")
  fi

  # Build type (single-config generators like Ninja/Unix Makefiles)
  # For Visual Studio/Xcode multi-config, this is mostly ignored but harmless.
  cmake_args+=(-DCMAKE_BUILD_TYPE="${BUILD_TYPE}")

  # macOS arch override (only if on macOS and provided)
  if [[ "$(uname -s)" == "Darwin" && -n "${MACOS_ARCH}" ]]; then
    cmake_args+=(-DCMAKE_OSX_ARCHITECTURES="${MACOS_ARCH}")
  fi

  cmake "${cmake_args[@]}"
}

build() {
  echo "==> Building (${BUILD_TYPE})..."
  cmake --build "${BUILD_DIR}" -j ${VERBOSE:+--verbose}
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

  # Use --config for safety (works for multi-config too)
  cmake --install "${BUILD_DIR}" --config "${BUILD_TYPE}" --prefix "${PREFIX}"

  # Sanity checks (non-fatal, but helpful)
  if [[ ! -d "${PREFIX}/include" ]]; then
    echo "WARN: ${PREFIX}/include not found. Did you add install(DIRECTORY include/ ...) rules?"
  fi
  if [[ ! -d "${PREFIX}/lib" && ! -d "${PREFIX}/lib64" ]]; then
    echo "WARN: ${PREFIX}/lib or lib64 not found. Did you add install(TARGETS ...) rules?"
  fi
  if [[ ! -d "${PREFIX}/lib/cmake/LiveKit" && ! -d "${PREFIX}/lib64/cmake/LiveKit" ]]; then
    echo "WARN: CMake package files not found under lib/cmake/LiveKit. Did you add install(EXPORT ...) + LiveKitConfig.cmake?"
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

cmd="$1"
case "${cmd}" in
  debug)
    BUILD_TYPE="Debug"
    parse_opts "$@"
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
    parse_opts "$@"
    configure
    build
    if [[ "${DO_BUNDLE}" == "1" ]]; then
      install_bundle
      if [[ "${DO_ARCHIVE}" == "1" ]]; then
        archive_bundle
      fi
    fi
    ;;
  verbose)
    VERBOSE="1"
    # Optional: allow --bundle with verbose builds as well, but requires configure already ran.
    parse_opts "$@"
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

