#!/usr/bin/env bash
set -euo pipefail

if ! command -v codex >/dev/null 2>&1; then
  echo "codex CLI not found in PATH" >&2
  exit 1
fi

MODEL="${FOSSIL_AGENT_MODEL:-auto}"
WORKDIR="${FOSSIL_AGENT_WORKDIR:-$PWD}"
SANDBOX="${FOSSIL_AGENT_CODEX_SANDBOX:-read-only}"

prompt_file="$(mktemp)"
out_file="$(mktemp)"
log_file="$(mktemp)"
trap 'rm -f "$prompt_file" "$out_file" "$log_file"' EXIT

cat >"$prompt_file"

cmd=(
  codex exec
  --cd "$WORKDIR"
  --skip-git-repo-check
  --sandbox "$SANDBOX"
  --color never
  --ephemeral
  --output-last-message "$out_file"
  -
)

if [ -n "$MODEL" ] && [ "$MODEL" != "auto" ]; then
  cmd=(
    codex exec
    --model "$MODEL"
    --cd "$WORKDIR"
    --skip-git-repo-check
    --sandbox "$SANDBOX"
    --color never
    --ephemeral
    --output-last-message "$out_file"
    -
  )
fi

if ! "${cmd[@]}" <"$prompt_file" >"$log_file" 2>&1; then
  cat "$log_file"
  exit 1
fi

if [ -s "$out_file" ]; then
  cat "$out_file"
else
  cat "$log_file"
fi
