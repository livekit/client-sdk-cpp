#!/usr/bin/env bash
# Collect unstripped test binaries, shared libraries, and core dumps for upload.
set -euo pipefail

build_dir=${1:?usage: stage_crash_diagnostics.sh <build-dir>}
staging="${RUNNER_TEMP}/crash-diagnostics"

rm -rf "${staging}"
mkdir -p "${staging}/bin" "${staging}/lib" "${staging}/cores"

shopt -s nullglob
for bin in "${build_dir}"/bin/livekit_*; do
  if [[ -f "${bin}" && -x "${bin}" ]]; then
    cp -a "${bin}" "${staging}/bin/"
  fi
done

for lib in "${build_dir}"/lib/liblivekit.*; do
  if [[ -f "${lib}" ]]; then
    cp -a "${lib}" "${staging}/lib/"
  fi
done

while IFS= read -r -d '' ffi_lib; do
  cp -a "${ffi_lib}" "${staging}/lib/"
done < <(find client-sdk-rust/target/debug -name 'liblivekit_ffi.*' -print0 2>/dev/null)

core_dir="${RUNNER_TEMP}/livekit-test-cores"
if [[ -d "${core_dir}" ]]; then
  find "${core_dir}" -maxdepth 1 -name 'core.*' -type f -exec cp -a {} "${staging}/cores/" \; 2>/dev/null || true
fi

if [[ "$(uname -s)" == "Darwin" && -d /cores ]]; then
  find /cores -maxdepth 1 -name 'core.*' -type f -exec cp -a {} "${staging}/cores/" \; 2>/dev/null || true
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
  mkdir -p "${staging}/crash-reports"
  for report_dir in "${HOME}/Library/Logs/DiagnosticReports" "/Library/Logs/DiagnosticReports"; do
    if [[ -d "${report_dir}" ]]; then
      find "${report_dir}" -maxdepth 1 -name '*.ips' -type f -exec cp -a {} "${staging}/crash-reports/" \; 2>/dev/null || true
    fi
  done
fi

echo "Staged crash diagnostics under ${staging}:"
find "${staging}" -type f -print
