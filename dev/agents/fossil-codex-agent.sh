#!/usr/bin/env bash
set -euo pipefail

resolve_codex_bin() {
  if [ -n "${FOSSIL_AGENT_CODEX_BIN:-}" ] && [ -x "${FOSSIL_AGENT_CODEX_BIN}" ]; then
    printf '%s\n' "${FOSSIL_AGENT_CODEX_BIN}"
    return 0
  fi
  if command -v codex >/dev/null 2>&1; then
    command -v codex
    return 0
  fi
  local zBin
  zBin="$(ls -d /home/bensiv/.vscode-server/extensions/openai.chatgpt-*/bin/linux-x86_64/codex 2>/dev/null | sort -V | tail -n 1 || true)"
  if [ -n "$zBin" ] && [ -x "$zBin" ]; then
    printf '%s\n' "$zBin"
    return 0
  fi
  return 1
}

CODEX_BIN="$(resolve_codex_bin || true)"
if [ -z "$CODEX_BIN" ]; then
  echo "codex CLI not found in PATH or known install locations" >&2
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
  "$CODEX_BIN" exec
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
    "$CODEX_BIN" exec
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
