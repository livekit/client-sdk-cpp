#!/usr/bin/env bash
# Run a test binary under debug CI. On fatal signals, print post-mortem
# backtraces from core dumps when available. Linux also runs under catchsegv
# so a partial backtrace appears in the log even without a core file.
set -uo pipefail

usage() {
  echo "Usage: $0 <test-binary> [gtest-args...]" >&2
  exit 2
}

[[ $# -ge 1 ]] || usage

binary=$1
shift

if [[ ! -x "$binary" ]]; then
  echo "Error: not executable: $binary" >&2
  exit 2
fi

binary_abs=$(cd "$(dirname "$binary")" && pwd)/$(basename "$binary")
core_dir="${RUNNER_TEMP:-/tmp}/livekit-test-cores"
mkdir -p "$core_dir"

ulimit -c unlimited || true

if [[ "$(uname -s)" == "Linux" ]]; then
  echo "${core_dir}/core.%e.%p" | sudo tee /proc/sys/kernel/core_pattern >/dev/null || true
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
  ulimit -c unlimited || true
  sudo sysctl -w kern.coredump=1 >/dev/null 2>&1 || true
  sudo mkdir -p /cores 2>/dev/null || true
  sudo chmod 1777 /cores 2>/dev/null || true
fi

dump_backtraces() {
  local test_pid=$1
  local status=$2

  echo "=== crash diagnostics (exit status ${status}, pid ${test_pid}) ==="

  if [[ "$(uname -s)" == "Linux" ]]; then
    local core=""
    core=$(find "$core_dir" -maxdepth 1 -name 'core.*' -type f 2>/dev/null | sort -r | head -1)
    if [[ -z "$core" ]]; then
      core=$(find /tmp -maxdepth 1 -name 'core.*' -type f 2>/dev/null | sort -r | head -1)
    fi
    if [[ -n "$core" && -f "$core" ]]; then
      echo "Core file: ${core}"
      if command -v gdb >/dev/null 2>&1; then
        gdb -batch \
          -ex 'set pagination off' \
          -ex 'thread apply all bt full' \
          "${binary_abs}" "${core}" || true
      else
        echo "gdb not available; install gdb for post-mortem backtraces"
      fi
      cp -a "${core}" "${core_dir}/" 2>/dev/null || true
      basename "${core}" >"${core_dir}/last-core.name"
    else
      echo "No core file found under ${core_dir} or /tmp"
    fi
    return 0
  fi

  if [[ "$(uname -s)" == "Darwin" ]]; then
    local core=""
    for candidate in "/cores/core.${test_pid}" "/cores/core.${test_pid}.dump"; do
      if [[ -f "${candidate}" ]]; then
        core=${candidate}
        break
      fi
    done
    if [[ -z "$core" ]]; then
      core=$(find /cores -maxdepth 1 -name "core.*" -type f 2>/dev/null | sort -r | head -1)
    fi
    if [[ -n "$core" && -f "$core" ]]; then
      echo "Core file: ${core}"
      if command -v lldb >/dev/null 2>&1; then
        lldb -b -c "${core}" -o 'thread backtrace all' -o 'quit' -- "${binary_abs}" || true
      else
        echo "lldb not available"
      fi
      cp -a "${core}" "${core_dir}/" 2>/dev/null || true
      basename "${core}" >"${core_dir}/last-core.name"
    else
      echo "No core file found under /cores for pid ${test_pid}"
    fi
  fi
}

run_test() {
  if [[ "$(uname -s)" == "Linux" ]] && command -v catchsegv >/dev/null 2>&1; then
    catchsegv "${binary_abs}" "$@"
  else
    "${binary_abs}" "$@"
  fi
}

parse_lldb_exit() {
  local lldb_log=$1
  local exit_line=""
  exit_line=$(grep -E 'Process [0-9]+ (exited with status|stopped with signal|exited with signal)' "${lldb_log}" | tail -1 || true)
  if [[ -z "${exit_line}" ]]; then
    return 1
  fi

  if [[ "${exit_line}" == *"exited with status = "* ]]; then
    grep -Eo 'exited with status = [0-9]+' <<<"${exit_line}" | grep -Eo '[0-9]+$'
    return 0
  fi

  if [[ "${exit_line}" == *"signal = SIGSEGV"* ]] || [[ "${exit_line}" == *"(11)"* ]]; then
    echo 139
    return 0
  fi

  if [[ "${exit_line}" == *"signal = SIGABRT"* ]] || [[ "${exit_line}" == *"(6)"* ]]; then
    echo 134
    return 0
  fi

  return 1
}

parse_lldb_pid() {
  local lldb_log=$1
  grep -Eo 'Process [0-9]+' "${lldb_log}" | tail -1 | grep -Eo '[0-9]+'
}

set +e
if [[ "$(uname -s)" == "Darwin" ]] && command -v lldb >/dev/null 2>&1; then
  # GHA macOS runners rarely write /cores dumps; run under lldb so a backtrace
  # appears in the log even when no core file is produced.
  lldb_log="${core_dir}/lldb-last.log"
  lldb -b \
    -o run \
    -o 'thread backtrace all' \
    -o quit \
    -- "${binary_abs}" "$@" 2>&1 | tee "${lldb_log}"
  test_pid=$(parse_lldb_pid "${lldb_log}")
  status=$(parse_lldb_exit "${lldb_log}")
  if [[ -z "${status}" ]]; then
    status=1
  fi
  if [[ -z "${test_pid}" ]]; then
    test_pid=0
  fi
else
  run_test "$@" &
  test_pid=$!
  wait "${test_pid}"
  status=$?
fi
set -e

if ((status > 128)); then
  signal=$((status - 128))
  echo "Test process ${test_pid} terminated by signal ${signal}"
  dump_backtraces "${test_pid}" "${status}"
elif ((status != 0)); then
  echo "Test process exited with status ${status}"
fi

exit "${status}"
