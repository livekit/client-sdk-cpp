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

# Shared checkout logic keeps the two dependency updaters consistent.
source "${script_dir}/update-submodule-common.sh"
check_update_prerequisites

fetch_ref=""
if [[ "$hash_provided" == false ]]; then
  fetch_ref="refs/heads/${default_branch}"
fi
update_submodule cpp-example-collection "$examples_remote" "$fetch_ref"
