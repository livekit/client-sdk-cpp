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
#
# install-pre-commit.sh -- Install a git pre-commit hook that runs
# `scripts/clang-format.sh --fix` on staged C/C++ files. Re-stages any
# files the formatter rewrote so the commit includes the fixes.

set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
hook_path="${repo_root}/.git/hooks/pre-commit"

cat >"${hook_path}" <<'HOOK'
#!/bin/sh
# Auto-format staged C/C++ files using ./scripts/clang-format.sh --fix.
files=$(git diff --cached --name-only --diff-filter=ACMR \
  -- "*.c" "*.cc" "*.cpp" "*.cxx" "*.h" "*.hpp" "*.hxx")
[ -z "${files}" ] && exit 0
echo "${files}" | xargs ./scripts/clang-format.sh --fix
echo "${files}" | xargs git add
HOOK

chmod +x "${hook_path}"
echo "Installed pre-commit hook at ${hook_path}"
