#!/usr/bin/env bash
#
# clang-tidy.sh -- Run clang-tidy locally and in CI using the same file set
# and config. Picks up checks from the repo-root .clang-tidy automatically.
#
# Usage (from anywhere; the script self-anchors to the repo root):
#   ./scripts/clang-tidy.sh                  # run on full src/ tree
#   ./scripts/clang-tidy.sh -j 4             # override parallelism
#   ./scripts/clang-tidy.sh --github-actions # force GitHub Actions annotation mode
#   ./scripts/clang-tidy.sh -fix             # forwarded to run-clang-tidy
#
# In GitHub Actions (auto-detected via $GITHUB_ACTIONS=true, or forced with
# --github-actions), this script additionally:
#   - Emits ::warning/::error workflow commands so findings appear as PR file
#     annotations (yellow for warnings, red for errors). Severity comes from
#     clang-tidy itself -- errors are findings promoted by WarningsAsErrors
#     in .clang-tidy.
#   - Writes a short markdown summary to $GITHUB_STEP_SUMMARY.
#   - Exits non-zero only when run-clang-tidy does (i.e. only on errors);
#     warnings annotate but do not fail the build.
#
# Requires CMake to have generated build-release/compile_commands.json.
# Run once:  ./build.sh release   (configures, builds, generates protobuf headers)

set -euo pipefail

# Anchor every relative path (BUILD_DIR, clang-tidy.log, etc.) to the repo root
# regardless of how/where this script is invoked. Without this, calling the
# script from a subdirectory or via an absolute path would break the
# compile_commands.json / generated/ checks below.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
cd "${script_dir}/.."

BUILD_DIR="build-release"
# Positive match for top-level src/*.{c,cpp,cc,cxx}; negative lookahead excludes
# dep paths (_deps/, build-*/, -src/src/) and every other top-level dir. Python
# regex (PCRE-ish) supports lookahead; this regex is evaluated by run-clang-tidy.
FILE_REGEX='^(?!.*/(_deps|build-[^/]*|bridge|examples|client-sdk-rust|cpp-example-collection|vcpkg_installed|docker|docs|data)/).*/src/(?!tests/).*\.(c|cpp|cc|cxx)$'

CI_MODE=0
# Automatically detect CI mode if in GitHub actions environment
if [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
  CI_MODE=1
fi

forward_args=()
while (($#)); do
  case "$1" in
    --github-actions|--gh)
      CI_MODE=1
      shift
      ;;
    *)
      forward_args+=("$1")
      shift
      ;;
  esac
done

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
  echo "ERROR: ${BUILD_DIR}/compile_commands.json not found." >&2
  echo "Run: ./build.sh release   (configures + builds, generates protobuf headers)" >&2
  exit 1
fi

# Protobuf sanity check. `cmake --preset` only configures -- it doesn't invoke
# the Rust/protoc chain that writes livekit's generated headers into
# build-release/generated/. If those headers are missing or stale, every TU
# that #includes "room.pb.h" / "ffi.pb.h" / etc. fails with dozens of
# clang-diagnostic-error diagnostics (e.g. "no member named 'FrameMetadata' in
# namespace 'livekit::proto'") that drown out the real findings. Detect the
# "configured but never built" state early and point the user at ./build.sh.
proto_dir="${BUILD_DIR}/generated"
if [[ ! -d "${proto_dir}" ]] || ! compgen -G "${proto_dir}/*.pb.h" >/dev/null; then
  echo "ERROR: no generated protobuf headers found in ${proto_dir}/." >&2
  echo "clang-tidy needs .pb.h files that are produced during the build step," >&2
  echo "not by 'cmake --preset' alone. To generate them, run:" >&2
  echo "" >&2
  echo "  ./build.sh release       # or release-tests / release-all, as needed" >&2
  echo "" >&2
  echo "If you previously bumped client-sdk-rust, also run 'clean-all' first:" >&2
  echo "" >&2
  echo "  ./build.sh clean-all && ./build.sh release" >&2
  exit 1
fi

if ! command -v run-clang-tidy >/dev/null 2>&1; then
  echo "ERROR: run-clang-tidy not found in PATH." >&2
  echo "Install LLVM:  brew install llvm   (macOS)" >&2
  echo "               apt install clang-tools-NN   (Linux)" >&2
  exit 1
fi

# On macOS, the C++ standard library headers (<cstdint>, <string>, ...) live
# inside the active Xcode / Command Line Tools SDK rather than on the default
# include path. Homebrew's clang-tidy doesn't know where that SDK is, so
# without -isysroot it fails every TU with "'cstdint' file not found" before
# any real check runs. `xcrun --show-sdk-path` resolves the currently selected
# SDK and we forward it to every clang-tidy invocation via --extra-arg. Linux
# CI doesn't need this -- the system clang-tidy already finds libstdc++/libc++
# through its built-in resource dir.
extra_args=()
if [[ "$(uname)" == "Darwin" ]]; then
  sdk_path="$(xcrun --show-sdk-path 2>/dev/null || true)"
  if [[ -n "${sdk_path}" ]]; then
    extra_args+=(-extra-arg="-isysroot${sdk_path}")
  fi
fi

# run-clang-tidy parallelizes across TUs via -j. Default to one worker per
# logical CPU so local runs aren't artificially slow. `nproc` is the Linux
# coreutils tool; macOS doesn't ship it, so fall back to `sysctl hw.ncpu`,
# and finally to a conservative 4 if neither is available (e.g. a minimal
# container).
if command -v nproc >/dev/null 2>&1; then
  jobs=$(nproc)
else
  jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
fi

# -------- Begin GitHub Actions annotations --------

# Emit GitHub Actions workflow commands for each clang-tidy diagnostic line
# in the given log. Notes (`path:L:C: note: ...`) are deliberately skipped --
# they belong to the preceding warning/error and would produce noisy extra
# annotations. Severity (::warning vs ::error) mirrors clang-tidy's prefix.
emit_annotations() {
  local log="$1"
  local workspace="${GITHUB_WORKSPACE:-${PWD}}"
  local line path lineno col severity message check rel_path

  while IFS= read -r line; do
    [[ "${line}" =~ ^(.+):([0-9]+):([0-9]+):[[:space:]]+(warning|error):[[:space:]]+(.+)[[:space:]]\[([^]]+)\][[:space:]]*$ ]] || continue
    path="${BASH_REMATCH[1]}"
    lineno="${BASH_REMATCH[2]}"
    col="${BASH_REMATCH[3]}"
    severity="${BASH_REMATCH[4]}"
    message="${BASH_REMATCH[5]}"
    check="${BASH_REMATCH[6]}"
    # clang-tidy appends ",-warnings-as-errors" to the bracket suffix when a
    # diagnostic is promoted from warning to error via WarningsAsErrors. Strip
    # it so the title, docs link, and Top-checks bucket are identical for the
    # warning and error code paths -- severity is already conveyed by the
    # ::warning / ::error workflow command prefix below.
    check="${check%,-warnings-as-errors}"

    rel_path="${path#${workspace}/}"

    message="${message//$'%'/%25}"
    message="${message//$'\r'/%0D}"
    message="${message//$'\n'/%0A}"

    printf '::%s file=%s,line=%s,col=%s,title=clang-tidy (%s)::%s\n' \
      "${severity}" "${rel_path}" "${lineno}" "${col}" "${check}" "${message}"
  done < "${log}"
}

# Append a markdown summary (counts + top checks + full finding list) to
# $GITHUB_STEP_SUMMARY so the GitHub job page surfaces every finding without
# needing to scan the raw log. Counts require a [check-name] suffix so
# clang-tidy's own config-parse errors can't inflate the totals.
write_step_summary() {
  local log="$1"
  local summary_file="${GITHUB_STEP_SUMMARY:-}"
  [[ -n "${summary_file}" ]] || return 0

  local workspace="${GITHUB_WORKSPACE:-${PWD}}"
  local findings_tsv
  findings_tsv="$(mktemp -t tidy-findings.XXXXXX)"

  # Extract every real finding (severity must be followed by [check-name])
  # as tab-separated severity\tpath\tline\tcol\tcheck\tmessage. Use bash's
  # regex engine (same as emit_annotations) for portability -- BSD awk and
  # mawk don't support gawk's 3-argument match().
  local sline spath slineno scol sseverity smessage scheck
  while IFS= read -r sline; do
    [[ "${sline}" =~ ^(.+):([0-9]+):([0-9]+):[[:space:]]+(warning|error):[[:space:]]+(.+)[[:space:]]\[([^]]+)\][[:space:]]*$ ]] || continue
    spath="${BASH_REMATCH[1]#${workspace}/}"
    slineno="${BASH_REMATCH[2]}"
    scol="${BASH_REMATCH[3]}"
    sseverity="${BASH_REMATCH[4]}"
    smessage="${BASH_REMATCH[5]}"
    scheck="${BASH_REMATCH[6]}"
    # Strip the ",-warnings-as-errors" promotion marker so error rows bucket
    # under the same check name as their warning counterparts (see
    # emit_annotations for the rationale).
    scheck="${scheck%,-warnings-as-errors}"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
      "${sseverity}" "${spath}" "${slineno}" "${scol}" "${scheck}" "${smessage}" \
      >> "${findings_tsv}"
  done < "${log}"

  local warnings errors total
  warnings=$(awk -F'\t' '$1=="warning"{c++} END{print c+0}' "${findings_tsv}")
  errors=$(awk -F'\t' '$1=="error"{c++} END{print c+0}' "${findings_tsv}")
  total=$((warnings + errors))

  # Resolve a blob URL prefix so findings in the table below become clickable
  # links to github.com. Prefer TIDY_BLOB_SHA (set by the workflow to the PR
  # head SHA) over GITHUB_SHA -- on pull_request events GITHUB_SHA points at
  # the ephemeral refs/pull/N/merge commit, whose blob URLs stop resolving
  # once the PR closes. On push / workflow_dispatch / schedule runs
  # TIDY_BLOB_SHA is unset and we fall through to GITHUB_SHA, which is the
  # pushed / selected commit respectively. When neither is set (local runs),
  # repo_url stays empty and the file column renders as plain code.
  local repo_url=""
  if [[ -n "${GITHUB_SERVER_URL:-}" && -n "${GITHUB_REPOSITORY:-}" ]]; then
    local blob_sha="${TIDY_BLOB_SHA:-${GITHUB_SHA:-}}"
    if [[ -n "${blob_sha}" ]]; then
      repo_url="${GITHUB_SERVER_URL}/${GITHUB_REPOSITORY}/blob/${blob_sha}"
    fi
  fi

  # Render a check name as a markdown link to its official clang-tidy docs page
  # (mirrors what cpp-linter-action used to do). The canonical URL layout is
  #   https://clang.llvm.org/extra/clang-tidy/checks/<module>/<rest>.html
  # where <module> is everything up to the first '-'. Two categories don't
  # follow that layout:
  #   * clang-diagnostic-*  -- compiler diagnostics, no per-check doc page
  #   * clang-analyzer-*    -- static analyzer, documented on a single page
  # Those fall back to plain code formatting / the analyzer index page.
  check_link() {
    local name="$1"
    local module="${name%%-*}"
    local rest="${name#*-}"
    case "${name}" in
      clang-diagnostic-*)
        printf '`%s`' "${name}"
        ;;
      clang-analyzer-*)
        printf '[`%s`](https://clang.llvm.org/docs/analyzer/checkers.html)' "${name}"
        ;;
      *)
        printf '[`%s`](https://clang.llvm.org/extra/clang-tidy/checks/%s/%s.html)' \
          "${name}" "${module}" "${rest}"
        ;;
    esac
  }

  {
    echo "## Analysis results"
    echo
    echo "| Severity | Count |"
    echo "|----------|-------|"
    # Same GFM shortcodes used in the detailed findings table below, so the
    # count summary and per-finding severity column read consistently.
    echo "| :x: Error | ${errors} |"
    echo "| :warning: Warning | ${warnings} |"
    echo

    if (( total > 0 )); then
      echo "### Top checks"
      echo
      echo '| Check | Count |'
      echo '|-------|-------|'
      awk -F'\t' '{print $5}' "${findings_tsv}" \
        | sort | uniq -c | sort -rn | head -5 \
        | while read -r count name; do
            printf '| %s | %d |\n' "$(check_link "${name}")" "${count}"
          done
      echo

      echo "<details><summary>All ${total} findings</summary>"
      echo
      echo '| Severity | File | Check | Message |'
      echo '|----------|------|-------|---------|'
      # Stream errors first, then warnings, by running two awk passes over the
      # TSV. This preserves log-discovery order within each severity bucket
      # without relying on `sort -s` (whose stable-sort semantics differ
      # subtly between GNU and BSD coreutils).
      {
        awk -F'\t' '$1=="error"' "${findings_tsv}"
        awk -F'\t' '$1=="warning"' "${findings_tsv}"
      } | while IFS=$'\t' read -r sev path lineno col check msg; do
        msg="${msg//|/\\|}"
        local icon label file_cell
        # GFM emoji shortcodes render in step summaries; :x: (red X) and
        # :warning: (yellow triangle) are the closest visual analogs to
        # GitHub's native annotation pills shown in the PR review UI. The
        # title-cased label matches the count table at the top of the summary.
        if [[ "${sev}" == "error" ]]; then
          icon=':x:'
          label='Error'
        else
          icon=':warning:'
          label='Warning'
        fi
        # Link to github.com when we have a blob URL and a repo-relative path.
        # Absolute paths (leading '/') are system headers that leaked past the
        # note filter -- rendering them as a github.com link would 404, so
        # fall back to plain code formatting. GitHub blob anchors support
        # #L<line> but not columns, so the column is kept in the label only.
        if [[ -n "${repo_url}" && "${path}" != /* ]]; then
          file_cell="[\`${path}:${lineno}\`](${repo_url}/${path}#L${lineno})"
        else
          file_cell="\`${path}:${lineno}\`"
        fi
        printf '| %s %s | %s | %s | %s |\n' \
          "${icon}" "${label}" "${file_cell}" "$(check_link "${check}")" "${msg}"
      done
      echo
      echo "</details>"
      echo
    fi
  } >> "${summary_file}"

  rm -f "${findings_tsv}"
}

# --------- End GitHub Actions annotations ---------

# Capture clang-tidy's combined stdout+stderr to a stable, repo-local path so
# it can be re-parsed after the run (for annotations and the step summary) and
# re-read by the user afterwards (e.g. `grep misc-const clang-tidy.log`).
# `*.log` is gitignored so this file never gets committed. Each run overwrites
# the previous log via `tee` (no -a), keeping the path predictable.
log="clang-tidy.log"

set +e
# run-clang-tidy is a Python script that doesn't flush stdout in its per-file
# loop; it only flushes once at exit. When its stdout is a pipe (the `| tee`
# below), Python's stdio defaults to block-buffered mode, so diagnostics
# accumulate in an ~8 KB buffer and the user sees nothing until the run is
# essentially over. PYTHONUNBUFFERED=1 forces line/unbuffered writes so each
# file's findings appear as soon as that file's clang-tidy finishes.
PYTHONUNBUFFERED=1 run-clang-tidy \
  -p "${BUILD_DIR}" \
  -quiet \
  -j "${jobs}" \
  "${extra_args[@]}" \
  "${forward_args[@]}" \
  "${FILE_REGEX}" \
  2>&1 | tee "${log}"
rc="${PIPESTATUS[0]}"
set -e

if [[ "${CI_MODE}" == "1" ]]; then
  emit_annotations "${log}"
  write_step_summary "${log}"
fi

echo "Results written to: $(cd "$(dirname "${log}")" && pwd)/$(basename "${log}")"

exit "${rc}"
