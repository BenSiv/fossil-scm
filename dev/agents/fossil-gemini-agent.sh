#!/usr/bin/env bash
set -uo pipefail

GEMINI_BIN="${FOSSIL_AGENT_GEMINI_BIN:-gemini}"
MODEL="${FOSSIL_AGENT_MODEL:-auto}"
PROMPT_FLAG="${FOSSIL_AGENT_GEMINI_PROMPT_FLAG:---prompt}"
MODEL_FLAG="${FOSSIL_AGENT_GEMINI_MODEL_FLAG:---model}"
REPLY_MARKER="___FOSSIL_AGENT_REPLY___"

# No real TTY here (stdout is piped to this script), so gemini CLI's own
# terminal-capability probe (process.stdout.getColorDepth()) finds nothing
# and falls back to checking $TERM, which is otherwise unset in this
# subprocess -- causing its harmless-but-noisy "256-color support not
# detected" compatibility warning on every single call. Standard fix for
# headless CLI invocations, same as CI systems setting TERM to keep
# color-aware tools quiet when there's no real terminal to describe.
export TERM="${TERM:-xterm-256color}"

if ! command -v "$GEMINI_BIN" >/dev/null 2>&1; then
  echo "gemini CLI not found in PATH" >&2
  exit 1
fi

prompt_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$prompt_file" "$stderr_file"' EXIT
cat >"$prompt_file"

# agent_prepare_command (agent_runtime.c) merges this whole invocation's
# stderr into the captured chat reply (2>&1 at the popen2 call site) --
# gemini CLI's own startup diagnostics (color-support/ripgrep-availability
# checks, etc.) would otherwise show up mixed into the model's answer.
# Captured separately here and re-emitted on our own stdout ahead of
# $REPLY_MARKER, so json-default.th1 can split them back out into a
# distinct "warnings" field for the UI to show as a dismissible banner,
# instead of either losing them or polluting the reply text. Not a
# hardcoded list of known-benign messages to hide -- whatever gemini CLI
# happens to print on stderr passes through unfiltered, so this stays
# correct if it starts emitting different diagnostics later.
if [ -n "$MODEL" ] && [ "$MODEL" != "auto" ]; then
  reply="$("$GEMINI_BIN" "$MODEL_FLAG" "$MODEL" "$PROMPT_FLAG" "$(cat "$prompt_file")" 2>"$stderr_file")"
else
  reply="$("$GEMINI_BIN" "$PROMPT_FLAG" "$(cat "$prompt_file")" 2>"$stderr_file")"
fi
rc=$?

warnings="$(cat "$stderr_file")"
if [ -n "$warnings" ]; then
  printf '%s\n' "$warnings"
  printf '%s\n' "$REPLY_MARKER"
fi
printf '%s' "$reply"
exit "$rc"
