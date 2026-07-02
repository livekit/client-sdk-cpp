#!/usr/bin/env bash
# Periodically capture live-heap snapshots of a running process so a reachable
# (non-freed but still-referenced) memory growth can be attributed to a type and
# allocation stack. `leaks` only reports *unreachable* blocks and runs at exit;
# this instead samples the live heap mid-run, which is where the PlatformAudio
# retention shows up.
#
# Two complementary tools are used (both require inspecting another task, so they
# run under `sudo -n`; GitHub macOS runners allow passwordless sudo):
#   - `heap`           : summary of live allocations grouped by type/class/binary.
#                        Diffing successive summaries shows which category grows.
#   - `malloc_history` : per-allocation backtraces (needs MallocStackLogging in
#                        the target); captured only on the last few ticks because
#                        the output is large.
#
# Self-terminates when the target process exits.
set -uo pipefail

pattern=${1:?usage: heap_snapshots.sh <process-name> <out-dir> [interval_sec] [max_snaps]}
outdir=${2:?usage: heap_snapshots.sh <process-name> <out-dir> [interval_sec] [max_snaps]}
interval=${3:-25}
max_snaps=${4:-10}

mkdir -p "${outdir}"
self=$$
RSS_THRESHOLD_KB=${HEAP_SNAPSHOT_RSS_THRESHOLD_KB:-50000}

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "heap_snapshots: only supported on macOS" >&2
  exit 0
fi

sudo_ok=0
if sudo -n true >/dev/null 2>&1; then sudo_ok=1; fi
if (( ! sudo_ok )); then
  echo "heap_snapshots: passwordless sudo unavailable; heap/malloc_history need it" >&2
fi

# Pick the matching PID with the largest RSS. `pgrep -f` can also match this
# script and the run_tests wrapper shell, so wait for the real test binary to
# cross an RSS threshold before attaching heap/malloc_history.
pick_target() {
  local best="" best_rss=0 p rss
  for p in $(pgrep -f "${pattern}" 2>/dev/null); do
    [[ "${p}" == "${self}" ]] && continue
    rss=$(ps -o rss= -p "${p}" 2>/dev/null | tr -d ' ')
    [[ -z "${rss}" ]] && continue
    if (( rss > best_rss )); then best_rss=${rss}; best=${p}; fi
  done
  echo "${best} ${best_rss}"
}

# Wait up to 120s for the real binary (RSS over threshold) to come up. Fall back
# to the largest match seen if nothing crosses the threshold before timeout.
pid=""
for _ in $(seq 1 120); do
  read -r cand cand_rss <<< "$(pick_target)"
  if [[ -n "${cand}" ]]; then
    pid=${cand}
    (( cand_rss >= RSS_THRESHOLD_KB )) && break
  fi
  sleep 1
done
if [[ -z "${pid}" ]]; then
  echo "heap_snapshots: process matching '${pattern}' never appeared" >&2
  exit 0
fi
echo "heap_snapshots: tracking pid ${pid} (pattern '${pattern}')" >&2

snap=0
while kill -0 "${pid}" 2>/dev/null && (( snap < max_snaps )); do
  sleep "${interval}"
  kill -0 "${pid}" 2>/dev/null || break
  snap=$((snap + 1))
  rss=$(ps -o rss= -p "${pid}" 2>/dev/null | tr -d ' ')
  ts=$(date -u +%H%M%S)
  label=$(printf '%02d_t%s_rss%sk' "${snap}" "${ts}" "${rss:-0}")
  echo "heap_snapshots: snapshot ${label}" >&2

  if (( sudo_ok )); then
    sudo -n heap "${pid}" > "${outdir}/heap-${label}.txt" 2>&1 || true
    # malloc_history -allBySize sorts largest-first, so head keeps the biggest
    # offenders while bounding artifact size. Capture every tick so we always
    # have stacks even if the process exits/hangs before max_snaps.
    sudo -n malloc_history "${pid}" -allBySize 2>/dev/null \
      | head -400 > "${outdir}/mhist-${label}.txt" || true
  fi
done

echo "heap_snapshots: done (${snap} snapshots) for pid ${pid}" >&2
