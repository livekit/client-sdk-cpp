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
rust_dir="${repo_root}/client-sdk-rust"
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

When --hash is omitted, the script uses the commit tagged by the most recent
published, non-draft GitHub release in livekit/client-sdk-rust.
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

if ! command -v git >/dev/null 2>&1; then
  echo "ERROR: git not found in PATH" >&2
  exit 1
fi

if ! git -C "$repo_root" rev-parse --show-toplevel >/dev/null 2>&1; then
  echo "ERROR: ${repo_root} is not a Git worktree" >&2
  exit 1
fi

if [[ ! -e "${rust_dir}/.git" ]]; then
  echo "==> Initializing client-sdk-rust"
  git -C "$repo_root" submodule update --init client-sdk-rust
fi

if ! git -C "$rust_dir" diff --quiet ||
  ! git -C "$rust_dir" diff --cached --quiet; then
  echo "ERROR: client-sdk-rust has tracked changes; commit or stash them first" >&2
  exit 1
fi

fetch_ref="$requested_hash"
release_tag=""

if [[ "$hash_provided" == false ]]; then
  if ! command -v gh >/dev/null 2>&1; then
    echo "ERROR: gh not found in PATH; install GitHub CLI or provide --hash" >&2
    exit 1
  fi

  echo "==> Finding the most recent published Rust SDK release"
  release_tags="$(
    gh api --paginate "repos/${rust_repo}/releases?per_page=100" \
      --jq '.[] | select(.draft == false) | .tag_name'
  )"
  release_tag="${release_tags%%$'\n'*}"
  if [[ -z "$release_tag" ]]; then
    echo "ERROR: no published release found for ${rust_repo}" >&2
    exit 1
  fi

  tag_refs="$(
    git ls-remote "$rust_remote" \
      "refs/tags/${release_tag}" "refs/tags/${release_tag}^{}"
  )"
  while read -r tag_hash tag_ref; do
    if [[ "$tag_ref" == "refs/tags/${release_tag}^{}" ]]; then
      requested_hash="$tag_hash"
      break
    fi
    if [[ "$tag_ref" == "refs/tags/${release_tag}" ]]; then
      requested_hash="$tag_hash"
    fi
  done <<<"$tag_refs"

  if [[ -z "$requested_hash" ]]; then
    echo "ERROR: release tag '${release_tag}' was not found in ${rust_repo}" >&2
    exit 1
  fi
  fetch_ref="refs/tags/${release_tag}"
  echo "==> Latest release: ${release_tag} (${requested_hash})"
else
  requested_hash="$(printf '%s' "$requested_hash" | tr '[:upper:]' '[:lower:]')"
  echo "==> Requested Rust SDK commit: ${requested_hash}"
fi

echo "==> Fetching client-sdk-rust"
if [[ "$hash_provided" == false ]]; then
  git -C "$rust_dir" fetch --quiet origin "$fetch_ref"
elif [[ ${#requested_hash} -eq 40 ]]; then
  if ! git -C "$rust_dir" fetch --quiet origin "$fetch_ref"; then
    git -C "$rust_dir" fetch --quiet origin
  fi
else
  git -C "$rust_dir" fetch --quiet origin
fi

if ! resolved_hash="$(git -C "$rust_dir" rev-parse --verify "${requested_hash}^{commit}" 2>/dev/null)"; then
  echo "ERROR: '${requested_hash}' does not resolve to a client-sdk-rust commit" >&2
  exit 1
fi

current_hash="$(git -C "$rust_dir" rev-parse HEAD)"
if [[ "$current_hash" == "$resolved_hash" ]]; then
  echo "==> client-sdk-rust is already at ${resolved_hash}"
else
  git -C "$rust_dir" checkout --quiet --detach "$resolved_hash"
  echo "==> Updated client-sdk-rust: ${current_hash} -> ${resolved_hash}"
fi

echo "==> Synchronizing nested Rust SDK submodules"
git -C "$rust_dir" submodule sync --recursive
git -C "$rust_dir" submodule update --init --recursive

echo "==> Done. Review and commit the client-sdk-rust gitlink change."
