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

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "${script_dir}/.." && pwd -P)"
rust_repo="livekit/client-sdk-rust"
rust_remote="https://github.com/${rust_repo}.git"

usage() {
  cat <<'EOF'
Usage: ./scripts/update-rust-sdk.sh [--hash HASH]

Update the client-sdk-rust submodule used by the C++ SDK.

Options:
  --hash HASH
        Use the specified Rust SDK commit. You can use a short or full SHA-1
        hash.
  -h, --help
        Show this help and exit.

When --hash is omitted, the script uses the commit tagged by the most recently
published, non-draft GitHub release (including prereleases) in
livekit/client-sdk-rust (requires gh).
EOF
}

requested_hash=""
hash_provided=false

while (($#)); do
  case "$1" in
    --hash)
      shift
      if (($# == 0)); then
        echo "ERROR: --hash requires a value" >&2
        exit 2
      fi
      requested_hash="$1"
      hash_provided=true
      shift
      ;;
    --hash=*)
      requested_hash="${1#--hash=}"
      hash_provided=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "ERROR: unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ "$hash_provided" == true && ! "$requested_hash" =~ ^[0-9a-fA-F]{7,40}$ ]]; then
  echo "ERROR: --hash must be a 7- to 40-character hexadecimal Git SHA-1 hash" >&2
  exit 2
fi

# Shared checkout logic keeps the two dependency updaters consistent.
source "${script_dir}/update-submodule-common.sh"
check_update_prerequisites

if [[ "$hash_provided" == false ]]; then
  if ! command -v gh >/dev/null 2>&1; then
    echo "ERROR: gh not found in PATH; install GitHub CLI or provide --hash" >&2
    exit 1
  fi
  echo "==> Finding the most recently published Rust SDK release"
  # Sort across every page by publication time, including prereleases. API
  # list order and GitHub's designated "latest" release are different concepts.
  release_tag="$(gh api --hostname github.com --paginate --slurp \
    "repos/${rust_repo}/releases?per_page=100" \
    --jq '[.[][] | select(.draft == false and .published_at != null)] |
          sort_by(.published_at, .id) | last | .tag_name // empty')"
  if [[ -z "$release_tag" ]]; then
    echo "ERROR: no published release found for ${rust_repo}" >&2
    exit 1
  fi
  git check-ref-format "refs/tags/${release_tag}" >/dev/null
  fetch_ref="refs/tags/${release_tag}"
  echo "==> Selected release: ${release_tag}"
else
  fetch_ref=""
fi

update_submodule client-sdk-rust "$rust_remote" "$fetch_ref"
