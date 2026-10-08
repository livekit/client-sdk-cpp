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

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: memory-regression.sh [OPTIONS]

Run the Linux memory lifecycle regression tester and append a table to the
GitHub step summary when GITHUB_STEP_SUMMARY is set.

Options:
  --build-dir PATH            Build directory (default: build-release)
  --iterations N              Lifecycle cycles (default: 100)
  --warmup N                  Baseline cycle (default: 20)
  --max-rss-growth-kib N      Allowed RSS growth (default: 8192)
  --max-thread-growth N       Allowed thread growth (default: 0)
  -h, --help                  Show this help

Environment:
  MEMORY_REGRESSION_BUILD_DIR, MEMORY_REGRESSION_ITERATIONS,
  MEMORY_REGRESSION_WARMUP, MEMORY_REGRESSION_MAX_RSS_GROWTH_KIB,
  MEMORY_REGRESSION_MAX_THREAD_GROWTH, MALLOC_ARENA_MAX
EOF
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
build_dir="${MEMORY_REGRESSION_BUILD_DIR:-build-release}"
iterations="${MEMORY_REGRESSION_ITERATIONS:-100}"
warmup="${MEMORY_REGRESSION_WARMUP:-20}"
max_rss_growth_kib="${MEMORY_REGRESSION_MAX_RSS_GROWTH_KIB:-8192}"
max_thread_growth="${MEMORY_REGRESSION_MAX_THREAD_GROWTH:-0}"

while (($#)); do
  case "$1" in
    --build-dir) build_dir="$2"; shift 2 ;;
    --iterations) iterations="$2"; shift 2 ;;
    --warmup) warmup="$2"; shift 2 ;;
    --max-rss-growth-kib) max_rss_growth_kib="$2"; shift 2 ;;
    --max-thread-growth) max_thread_growth="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ "${build_dir}" != /* ]]; then
  build_dir="${repo_root}/${build_dir}"
fi
tester="${build_dir}/bin/livekit_memory_source_lifecycle_tester"
if [[ ! -x "${tester}" ]]; then
  echo "ERROR: ${tester} not found. Run ./build.sh release-tests first." >&2
  exit 1
fi

set +e
output="$(
  MALLOC_ARENA_MAX="${MALLOC_ARENA_MAX:-1}" "${tester}" \
    --iterations "${iterations}" \
    --warmup "${warmup}" \
    --max-rss-growth-kib "${max_rss_growth_kib}" \
    --max-thread-growth "${max_thread_growth}" 2>&1
)"
status=$?
set -e
printf '%s\n' "${output}"

report_regex='memory lifecycle: RSS ([0-9]+) -> ([0-9]+) KiB \((-?[0-9]+) KiB\), threads ([0-9]+) -> ([0-9]+) \((-?[0-9]+)\), verdict=(PASS|FAIL)'
if [[ -n "${GITHUB_STEP_SUMMARY:-}" && "${output}" =~ ${report_regex} ]]; then
  {
    echo "## Linux memory regression"
    echo
    echo "| Metric | Baseline | Final | Growth | Limit |"
    echo "|---|---:|---:|---:|---:|"
    echo "| RSS | ${BASH_REMATCH[1]} KiB | ${BASH_REMATCH[2]} KiB | ${BASH_REMATCH[3]} KiB | ${max_rss_growth_kib} KiB |"
    echo "| Threads | ${BASH_REMATCH[4]} | ${BASH_REMATCH[5]} | ${BASH_REMATCH[6]} | ${max_thread_growth} |"
    echo
    echo "**Verdict: ${BASH_REMATCH[7]}**"
  } >> "${GITHUB_STEP_SUMMARY}"
fi

exit "${status}"
