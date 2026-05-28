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

usage() {
  cat <<'EOF'
Usage: ./scripts/build-docs.sh [--version VERSION]

Build Doxygen HTML docs with a visible project version.

Options:
  --version VERSION
        Version to display in generated docs. Accepts either "0.1.0" or
        "v0.1.0"; the rendered Doxygen PROJECT_NUMBER will include "v".
  -h, --help
        Show this help and exit.

When --version is omitted, the script uses LIVEKIT_DOXYGEN_PROJECT_NUMBER if
already set. Otherwise it derives a local version from git describe.
EOF
}

version_arg=""

while (($#)); do
  case "$1" in
    --version)
      shift
      if (($# == 0)); then
        echo "ERROR: --version requires a value" >&2
        exit 2
      fi
      version_arg="$1"
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

if ! command -v doxygen >/dev/null 2>&1; then
  echo "ERROR: doxygen not found in PATH." >&2
  echo "Install: brew install doxygen graphviz  (macOS)" >&2
  echo "         apt install doxygen graphviz   (Linux)" >&2
  exit 1
fi

project_number="${LIVEKIT_DOXYGEN_PROJECT_NUMBER:-}"

if [[ -n "$version_arg" ]]; then
  version="${version_arg#v}"
  project_number="v${version}"
elif [[ -z "$project_number" ]]; then
  if git -C "$repo_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    described="$(git -C "$repo_root" describe --tags --dirty --always 2>/dev/null || true)"
  else
    described=""
  fi

  if [[ -z "$described" ]]; then
    project_number="v0.0.0-dev"
  elif [[ "$described" == v* ]]; then
    project_number="$described"
  else
    project_number="v0.0.0-dev+${described}"
  fi
elif [[ "$project_number" != v* ]]; then
  project_number="v${project_number}"
fi

if [[ -z "$project_number" ]]; then
  echo "ERROR: derived Doxygen project number is empty" >&2
  exit 1
fi

if [[ "$project_number" =~ [[:space:]] ]]; then
  echo "ERROR: Doxygen project number contains whitespace: '$project_number'" >&2
  exit 1
fi

echo "==> Building Doxygen docs with PROJECT_NUMBER=${project_number}"
cd "$repo_root"
LIVEKIT_DOXYGEN_PROJECT_NUMBER="$project_number" doxygen docs/Doxyfile

docs_index="${repo_root}/html/index.html"

echo "==> Docs written to ${docs_index}"

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  echo "project_number=${project_number}" >>"$GITHUB_OUTPUT"
fi
