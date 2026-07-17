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

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
shared_script="${repo_root}/cpp-tools/clang-format.sh"
config="${repo_root}/.clang-format"
expected_config="cpp-tools/.clang-format"

if [[ ! -x "${shared_script}" ]]; then
  echo "ERROR: cpp-tools/clang-format.sh is unavailable." >&2
  echo "Run: git submodule update --init cpp-tools" >&2
  exit 1
fi

if [[ ! -L "${config}" ]] || [[ "$(readlink "${config}")" != "${expected_config}" ]]; then
  echo "ERROR: the project .clang-format link is not installed." >&2
  echo "Run: ./cpp-tools/install.sh clang-format" >&2
  exit 1
fi

export CLANG_FORMAT_FIX_COMMAND="./scripts/clang-format.sh --fix"
exec "${shared_script}" \
  --repo-root "${repo_root}" \
  --path src \
  --path include \
  --path benchmarks \
  "$@"
