#!/usr/bin/env bash
set -euo pipefail

mode="${FOSSIL_AGENT_MODE:-}"
model="${FOSSIL_AGENT_MODEL:-}"
input="$(cat)"

case "$mode" in
  chat)
    printf 'test-agent model=%s\n' "$model"
    printf 'reply: %s\n' "$(printf '%s' "$input" | tr '\n' ' ' | sed 's/[[:space:]]\+/ /g; s/^ //; s/ $//')"
    ;;
  embed)
    printf '{"embedding":[0.125,0.25,0.5,1.0]}\n'
    ;;
  *)
    printf 'Error: unsupported test agent mode: %s\n' "$mode"
    exit 1
    ;;
esac
