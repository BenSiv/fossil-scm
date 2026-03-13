#!/usr/bin/env bash
set -euo pipefail

if ! command -v ollama >/dev/null 2>&1; then
  echo "ollama CLI not found in PATH" >&2
  exit 1
fi

MODEL="${FOSSIL_AGENT_MODEL:-qwen3.5:0.8b}"

exec ollama run "$MODEL"
