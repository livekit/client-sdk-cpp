#!/usr/bin/env bash
#
# Copyright 2026 LiveKit
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

# Runs a command under platform-appropriate crash diagnostics and prints a
# native backtrace if it crashes. Linux core files are inspected with GDB;
# macOS commands run under LLDB. The command's original exit code is preserved.
set -euo pipefail

if (($# == 0)); then
  echo "Usage: $0 COMMAND [ARG ...]" >&2
  exit 2
fi

platform="$(uname -s)"

# Keep this wrapper transparent on Windows and in unconfigured environments.
if [[ "${platform}" != "Linux" && "${platform}" != "Darwin" ]] || [[ "${LIVEKIT_CRASH_DIAGNOSTICS:-}" != "1" ]]; then
  exec "$@"
fi

if [[ "${platform}" == "Darwin" ]]; then
  set +e
  lldb --batch \
    -o run \
    -o 'script import lldb, os; os._exit(lldb.debugger.GetSelectedTarget().GetProcess().GetExitStatus())' \
    -k "process status" \
    -k "thread backtrace all" \
    -k 'script import lldb, os; t = lldb.debugger.GetSelectedTarget().GetProcess().GetSelectedThread(); os._exit(128 + t.GetStopReasonDataAtIndex(0))' \
    -- "$@"
  exit_code=$?
  set -e
  exit "${exit_code}"
fi

if [[ -z "${LIVEKIT_CORE_DIR:-}" ]]; then
  echo "LIVEKIT_CORE_DIR must be set when Linux crash diagnostics are enabled." >&2
  exit 2
fi

mkdir -p "${LIVEKIT_CORE_DIR}"
rm -f "${LIVEKIT_CORE_DIR}"/livekit-core.*
ulimit -c unlimited

set +e
"$@"
exit_code=$?
set -e

shopt -s nullglob
core_files=("${LIVEKIT_CORE_DIR}"/livekit-core.*)

if ((${#core_files[@]} > 0)); then
  for core_file in "${core_files[@]}"; do
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
      echo "::group::Native crash backtrace ($(basename "${core_file}"))"
    fi

    timeout 120s gdb --batch --quiet \
      -ex "set pagination off" \
      -ex "info threads" \
      -ex "thread apply all backtrace" \
      "$1" "${core_file}" || true

    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
      echo "::endgroup::"
    fi
    rm -f "${core_file}"
  done
elif ((exit_code >= 128)); then
  echo "::warning::Test process exited from a signal (${exit_code}), but no core file was produced."
fi

exit "${exit_code}"
