#!/usr/bin/env bash
#
# tidy.sh -- Run clang-tidy locally using the same file set and config as CI.
#
# Matches the file filter used by the cpp-linter GitHub Action in
# .github/workflows/builds.yml: only src/**/*.{c,cpp,cc,cxx} excluding
# src/tests/. Picks up checks from the repo-root .clang-tidy automatically.
#
# Usage:
#   ./tidy.sh                # run on full src/ tree
#   ./tidy.sh -j 4           # override parallelism
#   ./tidy.sh -fix           # auto-apply fixes (forwarded to run-clang-tidy)
#
# Requires CMake to have generated build-release/compile_commands.json.
# Run once:  cmake --preset macos-release   (or linux-release)

set -euo pipefail

BUILD_DIR="build-release"
# Positive match for top-level src/*.{c,cpp,cc,cxx}; negative lookahead excludes
# dep paths (_deps/, build-*/, -src/src/) and other top-level dirs that CI's
# cpp-linter `ignore:` list filters out. Python regex (PCRE-ish) supports
# lookahead; this regex is evaluated by run-clang-tidy.
FILE_REGEX='^(?!.*/(_deps|build-[^/]*|bridge|examples|client-sdk-rust|cpp-example-collection|vcpkg_installed|docker|docs|data)/).*/src/(?!tests/).*\.(c|cpp|cc|cxx)$'

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
  echo "ERROR: ${BUILD_DIR}/compile_commands.json not found." >&2
  echo "Run: cmake --preset macos-release  (or linux-release)" >&2
  exit 1
fi

if ! command -v run-clang-tidy >/dev/null 2>&1; then
  echo "ERROR: run-clang-tidy not found in PATH." >&2
  echo "Install LLVM:  brew install llvm   (macOS)" >&2
  echo "               apt install clang-tidy   (Linux)" >&2
  exit 1
fi

extra_args=()
if [[ "$(uname)" == "Darwin" ]]; then
  sdk_path="$(xcrun --show-sdk-path 2>/dev/null || true)"
  if [[ -n "${sdk_path}" ]]; then
    extra_args+=(-extra-arg="-isysroot${sdk_path}")
  fi
fi

if command -v nproc >/dev/null 2>&1; then
  jobs=$(nproc)
else
  jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
fi

run-clang-tidy \
  -p "${BUILD_DIR}" \
  -quiet \
  -j "${jobs}" \
  "${extra_args[@]}" \
  "$@" \
  "${FILE_REGEX}"
