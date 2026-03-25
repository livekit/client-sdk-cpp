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

# Generate a LiveKit access token via `lk` and set LIVEKIT_TOKEN (and LIVEKIT_URL)
# for your current shell session.
#
#   source examples/tokens/gen_and_set.bash --id PARTICIPANT_ID --room ROOM_NAME [--view-token]
#   eval "$(bash examples/tokens/gen_and_set.bash --id ID --room ROOM [--view-token])"
#
# Optional env: LIVEKIT_API_KEY, LIVEKIT_API_SECRET, LIVEKIT_VALID_FOR.

# When sourced, we must NOT enable errexit/pipefail on the interactive shell — a
# failing pipeline (e.g. sed|head SIGPIPE) or any error would close your terminal.

_sourced=0
if [[ -n "${BASH_VERSION:-}" ]] && [[ "${BASH_SOURCE[0]}" != "${0}" ]]; then
  _sourced=1
elif [[ -n "${ZSH_VERSION:-}" ]] && [[ "${ZSH_EVAL_CONTEXT:-}" == *:file* ]]; then
  _sourced=1
fi

_fail() {
  echo "gen_and_set.bash: $1" >&2
  if [[ "$_sourced" -eq 1 ]]; then
    return "${2:-1}"
  fi
  exit "${2:-1}"
}

_usage() {
  echo "Usage: ${0##*/} --id PARTICIPANT_IDENTITY --room ROOM_NAME [--view-token]" >&2
  echo "  --id           LiveKit participant identity (required)" >&2
  echo "  --room         Room name (required; not read from env)" >&2
  echo "  --view-token   Print the JWT to stderr after generating" >&2
}

if [[ "$_sourced" -eq 0 ]]; then
  set -euo pipefail
fi

_view_token=0
LIVEKIT_IDENTITY=""
LIVEKIT_ROOM="robo_room"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --view-token)
      _view_token=1
      shift
      ;;
    --id)
      if [[ $# -lt 2 ]]; then
        _usage
        _fail "--id requires a value" 2
      fi
      LIVEKIT_IDENTITY="$2"
      shift 2
      ;;
    --room)
      if [[ $# -lt 2 ]]; then
        _usage
        _fail "--room requires a value" 2
      fi
      LIVEKIT_ROOM="$2"
      shift 2
      ;;
    -h | --help)
      _usage
      if [[ "$_sourced" -eq 1 ]]; then
        return 0
      fi
      exit 0
      ;;
    *)
      _usage
      _fail "unknown argument: $1" 2
      ;;
  esac
done

if [[ -z "$LIVEKIT_IDENTITY" ]]; then
  _usage
  _fail "--id is required" 2
fi
if [[ -z "$LIVEKIT_ROOM" ]]; then
  _usage
  _fail "--room is required" 2
fi

LIVEKIT_API_KEY="${LIVEKIT_API_KEY:-devkey}"
LIVEKIT_API_SECRET="${LIVEKIT_API_SECRET:-secret}"
LIVEKIT_VALID_FOR="${LIVEKIT_VALID_FOR:-99999h}"
_grant_json='{"canPublish":true,"canSubscribe":true,"canPublishData":true}'

if ! command -v lk >/dev/null 2>&1; then
  _fail "'lk' CLI not found. Install: https://docs.livekit.io/home/cli/" 2
fi

# Run lk inside bash so --grant JSON (with embedded ") is safe when this file is
# sourced from zsh; zsh misparses --grant "$json" on the same line.
_out="$(
  bash -c '
    lk token create \
      --api-key "$1" \
      --api-secret "$2" \
      -i "$3" \
      --join \
      --valid-for "$4" \
      --room "$5" \
      --grant "$6" 2>&1
  ' _ "$LIVEKIT_API_KEY" "$LIVEKIT_API_SECRET" "$LIVEKIT_IDENTITY" \
    "$LIVEKIT_VALID_FOR" "$LIVEKIT_ROOM" "$_grant_json"
)"
_lk_st=$?
if [[ "$_lk_st" -ne 0 ]]; then
  echo "$_out" >&2
  _fail "lk token create failed" 1
fi

# Avoid sed|head pipelines (pipefail + SIGPIPE can kill a sourced shell).
LIVEKIT_TOKEN=""
LIVEKIT_URL=""
while IFS= read -r _line || [[ -n "${_line}" ]]; do
  if [[ "$_line" == "Access token: "* ]]; then
    LIVEKIT_TOKEN="${_line#Access token: }"
  elif [[ "$_line" == "Project URL: "* ]]; then
    LIVEKIT_URL="${_line#Project URL: }"
  fi
done <<< "$_out"

if [[ -z "$LIVEKIT_TOKEN" ]]; then
  echo "gen_and_set.bash: could not parse Access token from lk output:" >&2
  echo "$_out" >&2
  _fail "missing Access token line" 1
fi

if [[ "$_view_token" -eq 1 ]]; then
  echo "$LIVEKIT_TOKEN" >&2
fi

_apply() {
  export LIVEKIT_TOKEN
  export LIVEKIT_URL
}

_emit_eval() {
  printf 'export LIVEKIT_TOKEN=%q\n' "$LIVEKIT_TOKEN"
  [[ -n "$LIVEKIT_URL" ]] && printf 'export LIVEKIT_URL=%q\n' "$LIVEKIT_URL"
}

if [[ "$_sourced" -eq 1 ]]; then
  _apply
  echo "LIVEKIT_TOKEN and LIVEKIT_URL set for this shell." >&2
  [[ -n "$LIVEKIT_URL" ]] || echo "gen_and_set.bash: warning: no Project URL in output; set LIVEKIT_URL manually." >&2
else
  _emit_eval
  echo "gen_and_set.bash: for this shell run: source $0 --id ... --room ...   or: eval \"\$(bash $0 ...)\"" >&2
fi
