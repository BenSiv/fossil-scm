#!/usr/bin/env bash
set -euo pipefail

GEMINI_BIN="${FOSSIL_AGENT_GEMINI_BIN:-gemini}"
MODEL="${FOSSIL_AGENT_MODEL:-auto}"
PROMPT_FLAG="${FOSSIL_AGENT_GEMINI_PROMPT_FLAG:---prompt}"
MODEL_FLAG="${FOSSIL_AGENT_GEMINI_MODEL_FLAG:---model}"

if ! command -v "$GEMINI_BIN" >/dev/null 2>&1; then
  echo "gemini CLI not found in PATH" >&2
  exit 1
fi

prompt_file="$(mktemp)"
trap 'rm -f "$prompt_file"' EXIT
cat >"$prompt_file"

if [ -n "$MODEL" ] && [ "$MODEL" != "auto" ]; then
  exec "$GEMINI_BIN" "$MODEL_FLAG" "$MODEL" "$PROMPT_FLAG" "$(cat "$prompt_file")"
fi
exec "$GEMINI_BIN" "$PROMPT_FLAG" "$(cat "$prompt_file")"
