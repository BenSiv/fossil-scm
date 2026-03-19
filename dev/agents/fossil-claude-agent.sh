#!/usr/bin/env bash
set -euo pipefail

CLAUDE_BIN="${FOSSIL_AGENT_CLAUDE_BIN:-claude}"
MODEL="${FOSSIL_AGENT_MODEL:-auto}"
PROMPT_FLAG="${FOSSIL_AGENT_CLAUDE_PROMPT_FLAG:--p}"
MODEL_FLAG="${FOSSIL_AGENT_CLAUDE_MODEL_FLAG:---model}"

if ! command -v "$CLAUDE_BIN" >/dev/null 2>&1; then
  echo "claude CLI not found in PATH" >&2
  exit 1
fi

prompt_file="$(mktemp)"
trap 'rm -f "$prompt_file"' EXIT
cat >"$prompt_file"

if [ -n "$MODEL" ] && [ "$MODEL" != "auto" ]; then
  exec "$CLAUDE_BIN" "$MODEL_FLAG" "$MODEL" "$PROMPT_FLAG" "$(cat "$prompt_file")"
fi
exec "$CLAUDE_BIN" "$PROMPT_FLAG" "$(cat "$prompt_file")"
