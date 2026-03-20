#!/usr/bin/env bash
set -euo pipefail

if ! command -v ollama >/dev/null 2>&1; then
  echo "ollama CLI not found in PATH" >&2
  exit 1
fi

MODEL="${FOSSIL_AGENT_MODEL:-qwen3.5:0.8b}"

# 1. Daemon check
if ! timeout 2s ollama list >/dev/null 2>&1; then
  echo "Error: Ollama daemon is not responding. (Is it running?)" >&2
  exit 1
fi

# 2. Model check
if ! ollama list | awk '{print $1}' | grep -q "^${MODEL}\(:latest\)\?$"; then
  echo "Error: Model '$MODEL' not found locally. Run 'ollama pull $MODEL' first." >&2
  exit 1
fi

ollama run "$MODEL" 2>&1 | python3 -c '
import re, sys
text = sys.stdin.read()
# Strip ANSI/OSC escapes.
text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text)
text = re.sub(r"\x1b\][^\a]*(?:\a|\x1b\\)", "", text)
# Drop braille spinner glyphs and control chars.
text = re.sub(r"[\u2800-\u28ff]", "", text)
text = re.sub(r"[\x00-\x08\x0b\x0c\x0e-\x1f]", "", text)
# Remove trailing EOF noise.
text = re.sub(r"(?:\r?\n)?Error: EOF\s*$", "", text)
sys.stdout.write(text)
'
