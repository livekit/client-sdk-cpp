#!/usr/bin/env bash
# Periodically sample resource usage of a process matched by name and emit CSV.
#
# Tracks the metrics that reveal a native teardown/recreate leak in the
# PlatformAudio triage: resident memory, OS thread count, open file
# descriptors, and (macOS) mach-port count. Mach ports are the tell for a
# CoreAudio HAL client leak -- each ADM Init talks to coreaudiod over a mach
# port, so a port count that climbs across dispose/recreate cycles points at an
# ADM Terminate() that is not fully releasing HAL resources.
#
# Self-terminates when the target process exits, so it can be safely launched
# in the background ahead of a test run.
set -uo pipefail

pattern=${1:?usage: sample_process_resources.sh <process-name> <out-csv> [interval_sec]}
out=${2:?usage: sample_process_resources.sh <process-name> <out-csv> [interval_sec]}
interval=${3:-3}

echo "iso_time,elapsed_s,pid,rss_kb,threads,fds,mach_ports" > "${out}"

# Wait up to 60s for the process to appear.
pid=""
for _ in $(seq 1 60); do
  pid=$(pgrep -f "${pattern}" | head -1 || true)
  [[ -n "${pid}" ]] && break
  sleep 1
done
if [[ -z "${pid}" ]]; then
  echo "sampler: process matching '${pattern}' never appeared" >&2
  exit 0
fi

is_macos=0
[[ "$(uname -s)" == "Darwin" ]] && is_macos=1

# mach-port counting needs lsmp + root to inspect another task. GitHub macOS
# runners allow passwordless sudo; `sudo -n` fails fast (no prompt) elsewhere,
# in which case the mach_ports column is left blank rather than a misleading 0.
mach_ports_cmd=""
if (( is_macos )) && command -v lsmp >/dev/null 2>&1; then
  if lsmp -p "$$" >/dev/null 2>&1; then
    mach_ports_cmd="lsmp -p"
  elif sudo -n true >/dev/null 2>&1; then
    mach_ports_cmd="sudo -n lsmp -p"
  fi
fi

start=$(date +%s)
while kill -0 "${pid}" 2>/dev/null; do
  now=$(date +%s)
  elapsed=$((now - start))
  ts=$(date -u +%Y-%m-%dT%H:%M:%SZ)

  rss=$(ps -o rss= -p "${pid}" 2>/dev/null | tr -d ' ')

  if (( is_macos )); then
    # macOS: ps -M lists one line per thread (plus a header line).
    threads=$(ps -M -p "${pid}" 2>/dev/null | tail -n +2 | grep -c . || true)
    if [[ -n "${mach_ports_cmd}" ]]; then
      mach_ports=$(${mach_ports_cmd} "${pid}" 2>/dev/null | grep -c -E 'send|recv|port set|dead' || true)
    else
      mach_ports=""
    fi
  else
    threads=$(ps -o nlwp= -p "${pid}" 2>/dev/null | tr -d ' ')
    mach_ports=""
  fi

  fds=$(lsof -p "${pid}" 2>/dev/null | tail -n +2 | grep -c . || true)

  echo "${ts},${elapsed},${pid},${rss:-},${threads:-},${fds:-},${mach_ports:-}" >> "${out}"
  sleep "${interval}"
done

echo "sampler: process ${pid} exited; samples written to ${out}" >&2
