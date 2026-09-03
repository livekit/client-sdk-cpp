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
examples_dir="${repo_root}/cpp-example-collection"
examples_repo="livekit-examples/cpp-example-collection"
examples_remote="https://github.com/${examples_repo}.git"
default_branch="main"

usage() {
  cat <<'EOF'
Usage: ./scripts/update-cpp-example-collection.sh [--hash HASH]

Update the cpp-example-collection submodule used by the C++ SDK.

Options:
  --hash HASH
        Use the specified examples commit. You can use a short or full SHA-1
        hash.
  -h, --help
        Show this help and exit.

When --hash is omitted, the script uses the most recent commit on the main
branch of livekit-examples/cpp-example-collection.
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

if [[ ! -e "${examples_dir}/.git" ]]; then
  echo "==> Initializing cpp-example-collection"
  git -C "$repo_root" submodule update --init cpp-example-collection
fi

if ! git -C "$examples_dir" diff --quiet ||
  ! git -C "$examples_dir" diff --cached --quiet; then
  echo "ERROR: cpp-example-collection has tracked changes; commit or stash them first" >&2
  exit 1
fi

fetch_ref="$requested_hash"

if [[ "$hash_provided" == false ]]; then
  echo "==> Finding the most recent cpp-example-collection commit"
  branch_ref="refs/heads/${default_branch}"
  remote_ref="$(git ls-remote "$examples_remote" "$branch_ref")"
  read -r requested_hash resolved_ref <<<"$remote_ref"
  if [[ -z "$requested_hash" || "$resolved_ref" != "$branch_ref" ]]; then
    echo "ERROR: branch '${default_branch}' was not found in ${examples_repo}" >&2
    exit 1
  fi
  fetch_ref="$branch_ref"
  echo "==> Latest ${default_branch} commit: ${requested_hash}"
else
  requested_hash="$(printf '%s' "$requested_hash" | tr '[:upper:]' '[:lower:]')"
  echo "==> Requested examples commit: ${requested_hash}"
fi

echo "==> Fetching cpp-example-collection"
if [[ "$hash_provided" == false ]]; then
  git -C "$examples_dir" fetch --quiet origin "$fetch_ref"
elif [[ ${#requested_hash} -eq 40 ]]; then
  if ! git -C "$examples_dir" fetch --quiet origin "$fetch_ref"; then
    git -C "$examples_dir" fetch --quiet origin
  fi
else
  git -C "$examples_dir" fetch --quiet origin
fi

if ! resolved_hash="$(git -C "$examples_dir" rev-parse --verify "${requested_hash}^{commit}" 2>/dev/null)"; then
  echo "ERROR: '${requested_hash}' does not resolve to a cpp-example-collection commit" >&2
  exit 1
fi

current_hash="$(git -C "$examples_dir" rev-parse HEAD)"
if [[ "$current_hash" == "$resolved_hash" ]]; then
  echo "==> cpp-example-collection is already at ${resolved_hash}"
else
  git -C "$examples_dir" checkout --quiet --detach "$resolved_hash"
  echo "==> Updated cpp-example-collection: ${current_hash} -> ${resolved_hash}"
fi

echo "==> Synchronizing nested example submodules"
git -C "$examples_dir" submodule sync --recursive
git -C "$examples_dir" submodule update --init --recursive

echo "==> Done. Review and commit the cpp-example-collection gitlink change."
