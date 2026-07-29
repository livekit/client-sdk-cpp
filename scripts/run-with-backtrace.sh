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

# Runs a command normally, prints a live process stack when terminated, then
# prints a native backtrace from any core file left after crashing. Linux cores
# are inspected with GDB and macOS cores with LLDB, avoiding debugger
# interference with normal process behavior.
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

if [[ -z "${LIVEKIT_CORE_DIR:-}" ]]; then
  echo "LIVEKIT_CORE_DIR must be set when crash diagnostics are enabled." >&2
  exit 2
fi

mkdir -p "${LIVEKIT_CORE_DIR}"
rm -f "${LIVEKIT_CORE_DIR}"/livekit-core.*
ulimit -c unlimited

child_pid=""

# Invoked indirectly by the signal traps below.
# shellcheck disable=SC2329
dump_live_process() {
  if [[ -z "${child_pid}" ]] || ! kill -0 "${child_pid}" 2>/dev/null; then
    echo "::warning::Test process is no longer running; a live stack could not be captured."
    return
  fi

  if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
    echo "::group::Live test process stacks before termination"
  fi

  if [[ "${platform}" == "Linux" ]]; then
    timeout 30s gdb --batch --quiet \
      -ex "set pagination off" \
      -ex "info threads" \
      -ex "thread apply all backtrace" \
      -p "${child_pid}" || true
  else
    # `sample` is available on GitHub's macOS runners and does not require a
    # crash/core file. A short sample provides all thread stacks while leaving
    # enough of the runner's termination grace period to forward the signal.
    /usr/bin/sample "${child_pid}" 3 10 || true
  fi

  if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
    echo "::endgroup::"
  fi
}

# Invoked indirectly by the signal traps below.
# shellcheck disable=SC2329
terminate_child() {
  local signal_name="$1"
  local exit_status="$2"
  trap - TERM INT
  dump_live_process
  kill "-${signal_name}" "${child_pid}" 2>/dev/null || true
  wait "${child_pid}" 2>/dev/null || true
  exit "${exit_status}"
}

trap 'terminate_child TERM 143' TERM
trap 'terminate_child INT 130' INT

set +e
"$@" &
child_pid=$!
wait "${child_pid}"
exit_code=$?
set -e
trap - TERM INT

shopt -s nullglob
core_files=("${LIVEKIT_CORE_DIR}"/livekit-core.*)

if ((${#core_files[@]} > 0)); then
  for core_file in "${core_files[@]}"; do
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
      echo "::group::Native crash backtrace ($(basename "${core_file}"))"
    fi

    if [[ "${platform}" == "Linux" ]]; then
      timeout 120s gdb --batch --quiet \
        -ex "set pagination off" \
        -ex "info threads" \
        -ex "thread apply all backtrace" \
        "$1" "${core_file}" || true
    else
      lldb --batch \
        --file "$1" \
        --core "${core_file}" \
        -o "process status" \
        -o "thread list" \
        -o "thread backtrace all" || true
    fi

    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
      echo "::endgroup::"
    fi
    rm -f "${core_file}"
  done
elif ((exit_code >= 128)); then
  echo "::warning::Test process exited from a signal (${exit_code}), but no core file was produced."
fi

exit "${exit_code}"
