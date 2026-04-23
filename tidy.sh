#!/usr/bin/env bash
#
# tidy.sh -- Run clang-tidy locally and in CI using the same file set and
# config. Picks up checks from the repo-root .clang-tidy automatically.
#
# Usage:
#   ./tidy.sh                  # run on full src/ tree
#   ./tidy.sh -j 4             # override parallelism
#   ./tidy.sh --github-actions # force GitHub Actions annotation mode
#   ./tidy.sh -fix             # forwarded to run-clang-tidy
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
# Run once:  cmake --preset macos-release   (or linux-release)

set -euo pipefail

BUILD_DIR="build-release"
# Positive match for top-level src/*.{c,cpp,cc,cxx}; negative lookahead excludes
# dep paths (_deps/, build-*/, -src/src/) and every other top-level dir. Python
# regex (PCRE-ish) supports lookahead; this regex is evaluated by run-clang-tidy.
FILE_REGEX='^(?!.*/(_deps|build-[^/]*|bridge|examples|client-sdk-rust|cpp-example-collection|vcpkg_installed|docker|docs|data)/).*/src/(?!tests/).*\.(c|cpp|cc|cxx)$'

CI_MODE=0
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
  echo "Run: cmake --preset macos-release  (or linux-release)" >&2
  exit 1
fi

if ! command -v run-clang-tidy >/dev/null 2>&1; then
  echo "ERROR: run-clang-tidy not found in PATH." >&2
  echo "Install LLVM:  brew install llvm   (macOS)" >&2
  echo "               apt install clang-tools-NN   (Linux)" >&2
  exit 1
fi

extra_args=()
if [[ "$(uname)" == "Darwin" ]]; then
  sdk_path="$(xcrun --show-sdk-path 2>/dev/null || true)"
  if [[ -n "${sdk_path}" ]]; then
    extra_args+=(-extra-arg="-isysroot${sdk_path}")
  fi
fi

if command -v nproc >/dev/null 2>&1; then
  jobs=$(nproc)
else
  jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
fi

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

    rel_path="${path#${workspace}/}"

    message="${message//$'%'/%25}"
    message="${message//$'\r'/%0D}"
    message="${message//$'\n'/%0A}"

    printf '::%s file=%s,line=%s,col=%s,title=clang-tidy (%s)::%s\n' \
      "${severity}" "${rel_path}" "${lineno}" "${col}" "${check}" "${message}"
  done < "${log}"
}

# Append a small markdown summary (counts + top checks) to $GITHUB_STEP_SUMMARY
# so the GitHub job page surfaces totals without needing to scan the log.
write_step_summary() {
  local log="$1"
  local summary_file="${GITHUB_STEP_SUMMARY:-}"
  [[ -n "${summary_file}" ]] || return 0

  local warnings errors
  warnings=$(grep -Ec '^.+:[0-9]+:[0-9]+:[[:space:]]+warning:[[:space:]]' "${log}" || true)
  errors=$(grep -Ec '^.+:[0-9]+:[0-9]+:[[:space:]]+error:[[:space:]]' "${log}" || true)

  {
    echo "## clang-tidy results"
    echo
    echo "| Severity | Count |"
    echo "|----------|-------|"
    echo "| Errors   | ${errors} |"
    echo "| Warnings | ${warnings} |"
    echo

    if (( warnings + errors > 0 )); then
      echo "### Top checks"
      echo
      echo '| Check | Count |'
      echo '|-------|-------|'
      grep -Eo '\[[a-zA-Z0-9._,-]+\]$' "${log}" \
        | sort | uniq -c | sort -rn | head -5 \
        | awk '{ n = $1; $1 = ""; sub(/^ /, ""); gsub(/[\[\]]/, "", $0); printf("| `%s` | %d |\n", $0, n) }'
      echo
    fi
  } >> "${summary_file}"
}

log="$(mktemp -t tidy-log.XXXXXX)"
trap 'rm -f "${log}"' EXIT

set +e
run-clang-tidy \
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

exit "${rc}"
