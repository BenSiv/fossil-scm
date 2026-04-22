#!/usr/bin/env bash
set -euo pipefail

API_KEY="${FOSSIL_AGENT_OPENAI_API_KEY:-${OPENAI_API_KEY:-}}"
BASE_URL="${FOSSIL_AGENT_OPENAI_BASE_URL:-https://api.openai.com/v1}"
MODEL="${FOSSIL_AGENT_MODEL:-text-embedding-3-small}"

if [ -z "$API_KEY" ]; then
  echo "OPENAI_API_KEY or FOSSIL_AGENT_OPENAI_API_KEY is required for Codex embeddings" >&2
  exit 1
fi

if [ -z "$MODEL" ] || [ "$MODEL" = "auto" ]; then
  MODEL="text-embedding-3-small"
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 is required for Codex embeddings" >&2
  exit 1
fi

python3 - "$API_KEY" "$BASE_URL" "$MODEL" <<'PY'
import json
import sys
import urllib.error
import urllib.request

api_key, base_url, model = sys.argv[1:4]
text = sys.stdin.read()
payload = json.dumps({
    "model": model,
    "input": text,
}).encode("utf-8")
req = urllib.request.Request(
    base_url.rstrip("/") + "/embeddings",
    data=payload,
    headers={
        "Authorization": f"Bearer {api_key}",
        "Content-Type": "application/json",
    },
    method="POST",
)
try:
    with urllib.request.urlopen(req) as resp:
        sys.stdout.write(resp.read().decode("utf-8"))
except urllib.error.HTTPError as exc:
    body = exc.read().decode("utf-8", "replace")
    sys.stderr.write(body + "\n")
    raise SystemExit(1)
PY
