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

#
# clang-format.sh -- Run clang-format locally and in CI using the same file
# set and config. Picks up style from the repo-root .clang-format
# automatically.
#
# Usage (from anywhere; the script self-anchors to the repo root):
#   ./scripts/clang-format.sh                          # check the full src/ tree
#   ./scripts/clang-format.sh --fix                    # apply formatting in place
#   ./scripts/clang-format.sh src/room.cpp src/foo.h   # check just these files
#   ./scripts/clang-format.sh --fix src/room.cpp       # fix just this file
#   ./scripts/clang-format.sh --github-actions         # force CI annotation mode
#
# Default mode is "check" (clang-format --dry-run --Werror): no files are
# modified and the script exits non-zero if any file diverges from
# .clang-format. Pass --fix / -i to apply formatting in place instead.
#
# Every run prints a concise stdout summary at the end with the number of
# files checked / fixed (and the list of offending paths when checking).
#
# In GitHub Actions (auto-detected via $GITHUB_ACTIONS=true, or forced with
# --github-actions), this script additionally:
#   - Emits ::error workflow commands so violations appear as PR file
#     annotations (red).
#   - Writes a markdown summary to $GITHUB_STEP_SUMMARY listing every file
#     that needs formatting, linkified to github.com when possible.
#
# Exit code:
#   - 0 when every file is already formatted (check mode) or when --fix
#     completed successfully.
#   - 1 when at least one file needs formatting (check mode only).
#   - The clang-format / xargs exit code on any other failure (missing tool,
#     unreadable file, etc.).

set -euo pipefail

# Anchor every relative path (clang-format.log, git ls-files, etc.) to the
# repo root regardless of how/where this script is invoked. Without this,
# calling the script from a subdirectory or via an absolute path would break
# git ls-files scoping and the log path below.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
cd "${script_dir}/.."

# Usage banner. Printed by --help and referenced by the EXIT trap below so
# users who hit a pre-flight error or a bad argument get pointed at this.
usage() {
  cat <<'EOF'
Usage: ./scripts/clang-format.sh [OPTIONS] [FILE...]

Run clang-format locally or in CI using the repo-root .clang-format. Defaults
to check-only (dry-run); pass --fix to rewrite files in place.

Options:
  -h, --help, -?
        Show this help and exit.
  --fix, -i
        Apply formatting in place. Without this, the script runs in check
        mode (--dry-run --Werror) and exits non-zero on any divergence.
  --github-actions, --gh
        Force GitHub Actions annotation + step-summary mode.
        Auto-detected when GITHUB_ACTIONS=true.

Positional arguments:
  FILE...
        Explicit list of files to format / check. When omitted, the script
        walks the tracked src/ tree (excluding src/tests/) and operates on
        every C/C++ source and header it finds. Pass paths to make the
        script work as a precommit hook on a curated set of files, e.g.:
            git diff --cached --name-only --diff-filter=ACMR \
              | grep -E '\.(c|cc|cpp|cxx|h|hpp|hxx)$' \
              | xargs ./scripts/clang-format.sh --fix

Examples:
  ./scripts/clang-format.sh                          # check full src/ tree
  ./scripts/clang-format.sh --fix                    # fix the same tree
  ./scripts/clang-format.sh src/room.cpp src/foo.h   # check just these files
  ./scripts/clang-format.sh --fix src/room.cpp       # fix just this file
  ./scripts/clang-format.sh --github-actions         # force CI annotations

Pre-requisites:
  clang-format must be installed and on PATH:
      brew install clang-format          # macOS
      apt install clang-format-19        # Linux (or any version >= 11)
EOF
}

# Print a one-line "see --help" hint on any non-zero exit during pre-flight
# (argument parsing, missing tool, etc.). Suppressed once we hand off to
# clang-format itself, so legitimate formatting violations don't trigger
# the hint.
__fmt_hint_active=1
__fmt_print_hint() {
  local rc=$?
  if (( rc != 0 )) && (( __fmt_hint_active )); then
    echo >&2
    echo "Run './scripts/clang-format.sh --help' for usage." >&2
  fi
}
trap __fmt_print_hint EXIT

CI_MODE=0
# Automatically detect CI mode if in GitHub Actions environment.
if [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
  CI_MODE=1
fi

# Default to check (dry-run) mode; --fix flips to in-place rewriting.
FIX_MODE=0

explicit_files=()
while (($#)); do
  case "$1" in
    -h|--help|-\?)
      usage
      __fmt_hint_active=0
      exit 0
      ;;
    --fix|-i)
      FIX_MODE=1
      shift
      ;;
    --github-actions|--gh)
      CI_MODE=1
      shift
      ;;
    --)
      # Explicit positional separator: everything after `--` is a file path.
      shift
      explicit_files+=("$@")
      break
      ;;
    --*|-*)
      # Long / short options we don't recognize are user typos far more often
      # than legitimate clang-format flags. Reject and surface usage.
      echo "ERROR: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      explicit_files+=("$1")
      shift
      ;;
  esac
done

if ! command -v clang-format >/dev/null 2>&1; then
  echo "ERROR: clang-format not found in PATH." >&2
  echo "Install:  brew install clang-format        (macOS)" >&2
  echo "          apt install clang-format-19      (Linux)" >&2
  exit 1
fi

# Surface the version in CI logs so future regressions tied to a specific
# clang-format release are easy to bisect.
clang-format --version

# Build the list of files to operate on. When the user passes explicit paths
# we honor them verbatim (no globbing, no src/tests/ filtering) so a precommit
# hook can supply its own staged-file list. Otherwise we walk the tracked
# src/ tree, excluding src/tests/. Using `git ls-files` automatically skips
# the client-sdk-rust/ submodule, build-*/, _deps/, local-install/, etc.,
# without having to maintain a lookahead exclusion regex.
files=()
if (( ${#explicit_files[@]} > 0 )); then
  files=("${explicit_files[@]}")
else
  while IFS= read -r -d '' path; do
    [[ "${path}" == src/tests/* ]] && continue
    files+=("${path}")
  done < <(git ls-files -z \
    'src/*.c' 'src/*.cc' 'src/*.cpp' 'src/*.cxx' \
    'src/*.h' 'src/*.hpp' 'src/*.hxx')
fi

file_count=${#files[@]}
if (( file_count == 0 )); then
  echo "clang-format: no files to process."
  __fmt_hint_active=0
  exit 0
fi

# xargs parallelism: one worker per logical CPU. `nproc` is the Linux
# coreutils tool; macOS doesn't ship it, so fall back to `sysctl hw.ncpu`,
# and finally to a conservative 4 if neither is available.
if command -v nproc >/dev/null 2>&1; then
  jobs=$(nproc)
else
  jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
fi

# Capture clang-format's combined stdout+stderr to a stable, repo-local path
# so it can be re-parsed after the run (for annotations and the step summary)
# and re-read by the user afterwards. `*.log` is gitignored so this file
# never gets committed.
log="clang-format.log"
: > "${log}"

# Past pre-flight: any non-zero exit from here on is a clang-format result
# (violations, internal error, etc.), not a user-facing argument/usage error,
# so suppress the "see --help" hint installed via the EXIT trap above.
__fmt_hint_active=0

# -------- Begin GitHub Actions annotations --------

# Emit GitHub Actions workflow commands for each clang-format diagnostic line
# in the given log. Source / caret lines are skipped; only the
# `path:line:col: error: ... [-Wclang-format-violations]` lines become
# annotations. Severity is always ::error because --Werror promotes every
# violation to an error and we treat any divergence as a hard CI failure.
emit_annotations() {
  local log_file="$1"
  local workspace="${GITHUB_WORKSPACE:-${PWD}}"
  local line path lineno col message rel_path

  while IFS= read -r line; do
    [[ "${line}" =~ ^(.+):([0-9]+):([0-9]+):[[:space:]]+(error|warning):[[:space:]]+(.+)[[:space:]]\[-Wclang-format-violations\][[:space:]]*$ ]] || continue
    path="${BASH_REMATCH[1]}"
    lineno="${BASH_REMATCH[2]}"
    col="${BASH_REMATCH[3]}"
    message="${BASH_REMATCH[5]}"

    rel_path="${path#${workspace}/}"

    message="${message//$'%'/%25}"
    message="${message//$'\r'/%0D}"
    message="${message//$'\n'/%0A}"

    printf '::error file=%s,line=%s,col=%s,title=clang-format::%s\n' \
      "${rel_path}" "${lineno}" "${col}" "${message}"
  done < "${log_file}"
}

# Append a markdown summary (count + per-file list) to $GITHUB_STEP_SUMMARY so
# the GitHub job page surfaces every offending file without scanning the raw
# log. Each file is linkified to github.com when we can resolve a blob URL.
write_step_summary() {
  local log_file="$1"
  local summary_file="${GITHUB_STEP_SUMMARY:-}"
  [[ -n "${summary_file}" ]] || return 0

  local workspace="${GITHUB_WORKSPACE:-${PWD}}"
  local files_tsv
  files_tsv="$(mktemp -t fmt-files.XXXXXX)"

  # Extract first violation line per file as path\tline. Multiple violations
  # in one file collapse to a single row -- clicking through to the file is
  # enough; the developer fixes them with `clang-format -i` regardless.
  local sline spath slineno
  declare -A seen=()
  while IFS= read -r sline; do
    [[ "${sline}" =~ ^(.+):([0-9]+):([0-9]+):[[:space:]]+(error|warning):[[:space:]]+(.+)[[:space:]]\[-Wclang-format-violations\][[:space:]]*$ ]] || continue
    spath="${BASH_REMATCH[1]#${workspace}/}"
    slineno="${BASH_REMATCH[2]}"
    if [[ -z "${seen[${spath}]:-}" ]]; then
      seen[${spath}]=1
      printf '%s\t%s\n' "${spath}" "${slineno}" >> "${files_tsv}"
    fi
  done < "${log_file}"

  local violation_files
  violation_files=$(wc -l < "${files_tsv}" | tr -d ' ')

  # Resolve a blob URL prefix so files in the table become clickable links.
  # Prefer FORMAT_BLOB_SHA (set by the workflow to the PR head SHA) over
  # GITHUB_SHA -- on pull_request events GITHUB_SHA points at the ephemeral
  # refs/pull/N/merge commit, whose blob URLs stop resolving once the PR
  # closes. On push / workflow_dispatch / schedule runs FORMAT_BLOB_SHA is
  # unset and we fall through to GITHUB_SHA, which is the pushed / selected
  # commit respectively.
  local repo_url=""
  if [[ -n "${GITHUB_SERVER_URL:-}" && -n "${GITHUB_REPOSITORY:-}" ]]; then
    local blob_sha="${FORMAT_BLOB_SHA:-${GITHUB_SHA:-}}"
    if [[ -n "${blob_sha}" ]]; then
      repo_url="${GITHUB_SERVER_URL}/${GITHUB_REPOSITORY}/blob/${blob_sha}"
    fi
  fi

  {
    echo "## clang-format results"
    echo
    if (( violation_files == 0 )); then
      echo ":white_check_mark: All files are properly formatted."
    else
      echo ":x: ${violation_files} file(s) need formatting."
      echo
      echo "<details><summary>Files needing formatting</summary>"
      echo
      echo '| File |'
      echo '|------|'
      while IFS=$'\t' read -r path lineno; do
        local file_cell
        if [[ -n "${repo_url}" && "${path}" != /* ]]; then
          file_cell="[\`${path}\`](${repo_url}/${path}#L${lineno})"
        else
          file_cell="\`${path}\`"
        fi
        printf '| %s |\n' "${file_cell}"
      done < "${files_tsv}"
      echo
      echo "</details>"
      echo
      echo "Run \`./scripts/clang-format.sh --fix\` locally to apply formatting."
    fi
  } >> "${summary_file}"

  rm -f "${files_tsv}"
}

# Print a one-line summary plus the offending file list to stdout. Always
# runs (regardless of CI_MODE) so local invocations get the same headline
# view the GitHub step summary provides. Sets a global consumed by the
# exit-code logic below: __FMT_VIOLATION_FILES.
print_stdout_summary() {
  local log_file="$1"
  local total="$2"
  local files_tsv
  files_tsv="$(mktemp -t fmt-stdout.XXXXXX)"

  local line spath
  declare -A seen=()
  while IFS= read -r line; do
    [[ "${line}" =~ ^(.+):([0-9]+):([0-9]+):[[:space:]]+(error|warning):[[:space:]]+(.+)[[:space:]]\[-Wclang-format-violations\][[:space:]]*$ ]] || continue
    spath="${BASH_REMATCH[1]}"
    if [[ -z "${seen[${spath}]:-}" ]]; then
      seen[${spath}]=1
      echo "${spath}" >> "${files_tsv}"
    fi
  done < "${log_file}"

  local violation_files
  violation_files=$(wc -l < "${files_tsv}" | tr -d ' ')

  echo "------------------------------------------------------------"
  if (( violation_files == 0 )); then
    printf 'clang-format summary: clean (%d file(s) checked)\n' "${total}"
  else
    printf 'clang-format summary: %d of %d file(s) need formatting\n' \
      "${violation_files}" "${total}"
    echo "  files:"
    while IFS= read -r f; do
      printf '    %s\n' "${f}"
    done < "${files_tsv}"
    echo
    echo "  Run './scripts/clang-format.sh --fix' to apply formatting."
  fi
  echo "------------------------------------------------------------"

  rm -f "${files_tsv}"
  __FMT_VIOLATION_FILES="${violation_files}"
}

# --------- End GitHub Actions annotations ---------

# Run clang-format. In fix mode, batch files across worker invocations for
# throughput; in check mode, hand each worker exactly one file so per-file
# diagnostics don't interleave across processes (clang-format writes one
# small diagnostic per violation, which on POSIX is atomic for writes
# <= PIPE_BUF, but only when each writer holds the fd alone for the whole
# message -- using -n 1 keeps each process's output contiguous in the log).
set +e
if (( FIX_MODE == 1 )); then
  printf '%s\0' "${files[@]}" \
    | xargs -0 -n 32 -P "${jobs}" clang-format -i \
      >"${log}" 2>&1
  rc=$?
else
  printf '%s\0' "${files[@]}" \
    | xargs -0 -n 1 -P "${jobs}" clang-format --dry-run --Werror \
      >"${log}" 2>&1
  rc=$?
fi
set -e

# Mirror the captured log to stdout so users see violations inline (the file
# is also kept on disk for re-parsing / re-reading after the run).
cat "${log}"

if [[ "${CI_MODE}" == "1" ]]; then
  emit_annotations "${log}"
  write_step_summary "${log}"
fi

echo "Results written to: $(pwd)/${log}"

# Always emit the concise headline summary to stdout. Sets
# __FMT_VIOLATION_FILES which the exit-code logic below consumes.
__FMT_VIOLATION_FILES=0
if (( FIX_MODE == 1 )); then
  echo "------------------------------------------------------------"
  printf 'clang-format summary: applied formatting to %d file(s)\n' "${file_count}"
  echo "------------------------------------------------------------"
else
  print_stdout_summary "${log}" "${file_count}"
fi

# Exit-code policy:
#   - In --fix mode, propagate clang-format's own exit code (non-zero means
#     a real failure such as a missing file, not "needed reformatting").
#   - In check mode, treat xargs's "child exited 1-125" status (123) as a
#     formatting violation -> exit 1. Any other non-zero status is a real
#     failure (missing file, IO error, etc.) and we propagate it as-is.
if (( FIX_MODE == 1 )); then
  exit "${rc}"
fi

if (( rc == 0 )); then
  exit 0
elif (( rc == 123 )) || (( __FMT_VIOLATION_FILES > 0 )); then
  exit 1
else
  exit "${rc}"
fi
