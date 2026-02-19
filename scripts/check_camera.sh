#!/usr/bin/env bash
#
# Copyright 2025 LiveKit
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
#
# check_camera.sh -- Debug script to verify camera availability on the device.
#
# Intended for troubleshooting when the bridge robot example reports
# "No video/camera device found" or "SDL camera init failed".
#
# Usage:
#   ./bridge/scripts/check_camera.sh [--verbose]
#
# On Linux, uses v4l2 devices and v4l2-ctl (if installed) to list cameras.
# The bridge uses SDL3 for capture; SDL3 typically uses v4l2 or PipeWire on Linux.

set -euo pipefail

VERBOSE=false
if [[ "${1:-}" == "--verbose" || "${1:-}" == "-v" ]]; then
  VERBOSE=true
fi

EXIT_OK=0
EXIT_NO_CAMERA=1
EXIT_OTHER=2

echo "=== Camera Debug Check ==="
echo ""

# --- Linux: check /dev/video* ---
if [[ "$(uname -s)" == "Linux" ]]; then
  VIDEO_DEVICES=()
  while IFS= read -r -d '' dev; do
    VIDEO_DEVICES+=("$dev")
  done < <(find /dev -maxdepth 1 -name 'video*' -print0 2>/dev/null | sort -z)

  if [[ ${#VIDEO_DEVICES[@]} -eq 0 ]]; then
    echo "  No /dev/video* devices found."
    echo ""
    echo "  Possible causes:"
    echo "    - Camera not connected or not recognized"
    echo "    - Kernel module not loaded (e.g. uvcvideo for USB webcams)"
    echo "    - Insufficient permissions (try: sudo or add user to 'video' group)"
    echo "    - On Jetson: ensure camera is enabled in device tree"
    echo ""
    exit $EXIT_NO_CAMERA
  fi

  echo "  Found ${#VIDEO_DEVICES[@]} video device(s):"
  for dev in "${VIDEO_DEVICES[@]}"; do
    echo "    $dev"
  done
  echo ""

  # --- v4l2-ctl for detailed info ---
  if command -v v4l2-ctl &>/dev/null; then
    for dev in "${VIDEO_DEVICES[@]}"; do
      echo "  --- $dev ---"
      if v4l2-ctl -d "$dev" --info 2>/dev/null; then
        echo ""
        if [[ "$VERBOSE" == true ]]; then
          echo "  Supported formats (sample):"
          v4l2-ctl -d "$dev" --list-formats-ext 2>/dev/null | head -40 || true
          echo ""
        fi
      else
        echo "    (v4l2-ctl failed - device may be in use or not a capture device)"
      fi
      echo ""
    done
  else
    echo "  Tip: Install v4l-utils for detailed device info:"
    echo "    sudo apt install v4l-utils   # Debian/Ubuntu"
    echo ""
  fi

  # --- Check video group / permissions ---
  for dev in "${VIDEO_DEVICES[@]}"; do
    if [[ -r "$dev" ]]; then
      echo "  $dev: readable by current user"
    else
      echo "  $dev: NOT readable (try: sudo chmod 666 $dev or add user to 'video' group)"
    fi
  done
  echo ""

  # --- PipeWire (SDL3 may use this on some setups) ---
  if [[ "$VERBOSE" == true ]] && command -v pw-cli &>/dev/null; then
    echo "  PipeWire video devices:"
    pw-cli list-objects Node 2>/dev/null | grep -E "node\.name|media\.(class|name)" || true
    echo ""
  fi

  echo "  SDL3 (used by bridge robot example) typically uses v4l2 or PipeWire."
  echo "  If devices exist but robot still fails, ensure SDL_INIT_VIDEO | SDL_INIT_CAMERA succeeds."
  echo ""
  exit $EXIT_OK
fi

# --- macOS ---
if [[ "$(uname -s)" == "Darwin" ]]; then
  echo "  macOS: SDL3 uses AVFoundation for cameras."
  echo "  Check System Settings > Privacy & Security > Camera for app permissions."
  if [[ "$VERBOSE" == true ]] && command -v system_profiler &>/dev/null; then
    echo ""
    echo "  Connected cameras (from system_profiler):"
    system_profiler SPCameraDataType 2>/dev/null || true
  fi
  echo ""
  exit $EXIT_OK
fi

# --- Other ---
echo "  Unsupported platform: $(uname -s)"
echo "  The bridge uses SDL3 for camera access. Run the robot example to test."
exit $EXIT_OK
