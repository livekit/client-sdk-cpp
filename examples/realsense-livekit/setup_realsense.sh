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

# Clones external dependencies and generates protobuf C++ files for the
# realsense-to-mcap example.  Safe to re-run (idempotent).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}/realsense-to-mcap"
EXTERNAL_DIR="${PROJECT_DIR}/external"
GENERATED_DIR="${PROJECT_DIR}/generated"

MCAP_REPO="https://github.com/foxglove/mcap.git"
FOXGLOVE_SDK_REPO="https://github.com/foxglove/foxglove-sdk.git"

# ---------------------------------------------------------------------------
# Detect platform
# ---------------------------------------------------------------------------
OS="$(uname -s)"
case "${OS}" in
  Linux*)  PLATFORM=linux  ;;
  Darwin*) PLATFORM=macos  ;;
  *)       PLATFORM=unknown ;;
esac

# ---------------------------------------------------------------------------
# Check system dependencies
# ---------------------------------------------------------------------------
check_cmd() {
  if ! command -v "$1" &>/dev/null; then
    echo "WARNING: '$1' not found. $2"
    return 1
  fi
  return 0
}

hint_install() {
  local pkg_apt="$1"
  local pkg_brew="$2"
  if [ "${PLATFORM}" = "macos" ]; then
    echo "Install via: brew install ${pkg_brew}"
  else
    echo "Install via: sudo apt install ${pkg_apt}"
  fi
}

missing=0
check_cmd protoc     "$(hint_install protobuf-compiler protobuf)" || missing=1
check_cmd pkg-config "$(hint_install pkg-config pkg-config)"      || missing=1

if pkg-config --exists realsense2 2>/dev/null; then
  echo "  realsense2 ... found"
else
  echo "WARNING: librealsense2 not found via pkg-config."
  hint_install librealsense2-dev librealsense
  missing=1
fi

if [ "$missing" -ne 0 ]; then
  echo ""
  echo "Some dependencies are missing (see warnings above). You can still"
  echo "continue, but the build may fail."
  echo ""
fi

# ---------------------------------------------------------------------------
# Clone / update external repos
# ---------------------------------------------------------------------------
mkdir -p "${EXTERNAL_DIR}"

clone_or_pull() {
  local repo_url="$1"
  local dest="$2"

  if [ -d "${dest}/.git" ]; then
    echo "Updating $(basename "${dest}") ..."
    git -C "${dest}" pull --ff-only
  else
    echo "Cloning $(basename "${dest}") ..."
    git clone --depth 1 "${repo_url}" "${dest}"
  fi
}

clone_or_pull "${MCAP_REPO}"        "${EXTERNAL_DIR}/mcap"
clone_or_pull "${FOXGLOVE_SDK_REPO}" "${EXTERNAL_DIR}/foxglove-sdk"

# ---------------------------------------------------------------------------
# Generate C++ protobuf files from Foxglove schemas
# ---------------------------------------------------------------------------
PROTO_DIR="${EXTERNAL_DIR}/foxglove-sdk/schemas/proto"

if [ ! -d "${PROTO_DIR}/foxglove" ]; then
  echo "ERROR: Proto schemas not found at ${PROTO_DIR}/foxglove"
  exit 1
fi

mkdir -p "${GENERATED_DIR}"

echo "Generating protobuf C++ sources ..."
protoc \
  --cpp_out="${GENERATED_DIR}" \
  -I "${PROTO_DIR}" \
  "${PROTO_DIR}"/foxglove/*.proto

echo ""
echo "Setup complete.  Generated files are in:"
echo "  ${GENERATED_DIR}"
echo ""
echo "To build:"
echo "  cd ${PROJECT_DIR}"
echo "  cmake -B build && cmake --build build"
