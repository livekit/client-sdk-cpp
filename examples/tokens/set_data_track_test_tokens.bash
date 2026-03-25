#!/usr/bin/env bash
# Copyright 2026 LiveKit, Inc.
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

# Generate two LiveKit access tokens via `lk` and set the environment variables
# required by src/tests/integration/test_data_track.cpp.
#
#   source examples/tokens/set_data_track_test_tokens.bash
#   eval "$(bash examples/tokens/set_data_track_test_tokens.bash)"
#
# Exports:
#   LK_TOKEN_TEST_A
#   LK_TOKEN_TEST_B
#   LIVEKIT_URL=ws://localhost:7880
#

_sourced=0
if [[ -n "${BASH_VERSION:-}" ]] && [[ "${BASH_SOURCE[0]}" != "${0}" ]]; then
  _sourced=1
elif [[ -n "${ZSH_VERSION:-}" ]] && [[ "${ZSH_EVAL_CONTEXT:-}" == *:file* ]]; then
  _sourced=1
fi

_fail() {
  echo "set_data_track_test_tokens.bash: $1" >&2
  if [[ "$_sourced" -eq 1 ]]; then
    return "${2:-1}"
  fi
  exit "${2:-1}"
}

if [[ "$_sourced" -eq 0 ]]; then
  set -euo pipefail
fi

LIVEKIT_ROOM="cpp_data_track_test"
LIVEKIT_IDENTITY_A="cpp-test-a"
LIVEKIT_IDENTITY_B="cpp-test-b"

if [[ $# -ne 0 ]]; then
  _fail "this script is hard-coded and does not accept arguments" 2
fi

LIVEKIT_API_KEY="devkey"
LIVEKIT_API_SECRET="secret"
LIVEKIT_VALID_FOR="99999h"
LIVEKIT_URL="ws://localhost:7880"
_grant_json='{"canPublish":true,"canSubscribe":true,"canPublishData":true}'

if ! command -v lk >/dev/null 2>&1; then
  _fail "'lk' CLI not found. Install: https://docs.livekit.io/home/cli/" 2
fi

_create_token() {
  local identity="$1"
  local output=""
  local command_status=0
  local token=""

  output="$(
    bash -c '
      lk token create \
        --api-key "$1" \
        --api-secret "$2" \
        -i "$3" \
        --join \
        --valid-for "$4" \
        --room "$5" \
        --grant "$6" 2>&1
    ' _ "$LIVEKIT_API_KEY" "$LIVEKIT_API_SECRET" "$identity" \
      "$LIVEKIT_VALID_FOR" "$LIVEKIT_ROOM" "$_grant_json"
  )"
  command_status=$?
  if [[ "$command_status" -ne 0 ]]; then
    echo "$output" >&2
    _fail "lk token create failed for identity '$identity'" 1
  fi

  while IFS= read -r line || [[ -n "${line}" ]]; do
    if [[ "$line" == "Access token: "* ]]; then
      token="${line#Access token: }"
      break
    fi
  done <<< "$output"

  if [[ -z "$token" ]]; then
    echo "$output" >&2
    _fail "could not parse Access token for identity '$identity'" 1
  fi

  printf '%s' "$token"
}

LK_TOKEN_TEST_A="$(_create_token "$LIVEKIT_IDENTITY_A")"
LK_TOKEN_TEST_B="$(_create_token "$LIVEKIT_IDENTITY_B")"

_apply() {
  export LK_TOKEN_TEST_A
  export LK_TOKEN_TEST_B
  export LIVEKIT_URL
}

_emit_eval() {
  printf 'export LK_TOKEN_TEST_A=%q\n' "$LK_TOKEN_TEST_A"
  printf 'export LK_TOKEN_TEST_B=%q\n' "$LK_TOKEN_TEST_B"
  printf 'export LIVEKIT_URL=%q\n' "$LIVEKIT_URL"
}

if [[ "$_sourced" -eq 1 ]]; then
  _apply
  echo "LK_TOKEN_TEST_A, LK_TOKEN_TEST_B, and LIVEKIT_URL set for this shell." >&2
else
  _emit_eval
  echo "set_data_track_test_tokens.bash: for this shell run: source $0   or: eval \"\$(bash $0 ...)\"" >&2
fi
