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

# Internal helpers sourced by update-rust-sdk.sh and
# update-cpp-example-collection.sh. Requires Bash 3.2 or newer.

check_update_prerequisites() {
  if ! command -v git >/dev/null 2>&1; then
    echo "ERROR: git not found in PATH" >&2
    exit 1
  fi
  local top_level
  top_level="$(git -C "$repo_root" rev-parse --show-toplevel)"
  if [[ "$(cd "$top_level" && pwd -P)" != "$repo_root" ]]; then
    echo "ERROR: ${repo_root} is not the SDK Git worktree root" >&2
    exit 1
  fi
}

require_clean_submodule() {
  local directory="$1"
  local changes
  # Explicitly override submodule.*.ignore and diff.ignoreSubmodules. This
  # includes nested HEAD changes, staged edits, and untracked files.
  changes="$(git -C "$directory" status --porcelain --untracked-files=all \
    --ignore-submodules=none)"
  if [[ -n "$changes" ]]; then
    echo "ERROR: ${directory} has local changes (possibly in nested submodules)." >&2
    echo "Commit, stash, or move them before updating:" >&2
    printf '%s\n' "$changes" >&2
    exit 1
  fi
}

update_submodule() {
  local name="$1" remote="$2" fetch_ref="$3"
  local directory="${repo_root}/${name}"
  local resolved_hash current_hash
  local fetch_options=(--no-tags)

  if [[ -e "${directory}/.git" ]]; then
    require_clean_submodule "$directory"
  else
    echo "==> Initializing ${name}"
    git -C "$repo_root" submodule update --init --checkout -- "$name"
    require_clean_submodule "$directory"
  fi

  echo "==> Fetching ${name} from ${remote}"
  # Use the same authoritative repository for resolution and fetching, even
  # when a developer's origin points to a fork. Leave their remotes unchanged.
  if [[ "$hash_provided" == false ]]; then
    git -C "$directory" fetch --quiet --no-tags "$remote" "$fetch_ref"
    resolved_hash="$(git -C "$directory" rev-parse --verify 'FETCH_HEAD^{commit}')"
  else
    requested_hash="$(printf '%s' "$requested_hash" | tr '[:upper:]' '[:lower:]')"
    if [[ ${#requested_hash} -lt 40 ]] ||
      ! git -C "$directory" fetch --quiet --no-tags "$remote" "$requested_hash"; then
      # Abbreviated hashes cannot be fetched directly. Fetch all branch/tag
      # objects without updating local branches or overwriting local tags.
      if [[ "$(git -C "$directory" rev-parse --is-shallow-repository)" == true ]]; then
        fetch_options+=(--unshallow)
      fi
      git -C "$directory" fetch --quiet "${fetch_options[@]}" "$remote" \
        '+refs/heads/*:refs/sdk-update/heads/*' '+refs/tags/*:refs/sdk-update/tags/*'
    fi
    if ! resolved_hash="$(git -C "$directory" rev-parse --verify "${requested_hash}^{commit}" 2>/dev/null)" ||
      [[ "$resolved_hash" != "$requested_hash"* ]]; then
      echo "ERROR: '${requested_hash}' does not unambiguously identify a ${name} commit" >&2
      exit 1
    fi
  fi

  current_hash="$(git -C "$directory" rev-parse HEAD)"
  echo "==> Selected ${name}: ${current_hash} -> ${resolved_hash}"
  # Failures here can leave a partial update. Do not attempt a destructive
  # rollback; show the developer exactly what needs inspection and retrying.
  trap 'echo "ERROR: Update incomplete. Inspect git status and git submodule status --recursive; fix the error and rerun the updater. No changes were staged or committed." >&2' ERR
  git -C "$directory" checkout --quiet --no-overwrite-ignore --detach "$resolved_hash"
  git -C "$directory" submodule sync --recursive
  git -C "$directory" submodule update --init --recursive --checkout
  trap - ERR

  echo "==> Done. Review with: git diff --submodule=log -- ${name}"
  echo "==> Stage when ready: git add -- ${name}"
}
