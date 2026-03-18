#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
LOG_DIR="$REPO_ROOT/log/build"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
SESSION_DIR="$LOG_DIR/install-fossil-clean-$TIMESTAMP"
MAKE_JOBS="${MAKE_JOBS:-4}"
SUDO_BIN="${SUDO_BIN-sudo}"
INSTALL_CMD="${INSTALL_CMD:-make install}"
TOTAL_STEPS=4
CURRENT_STEP=0

mkdir -p "$SESSION_DIR"

draw_progress() {
  local completed="$1"
  local total="$2"
  local label="$3"
  local width=28
  local filled=$((completed * width / total))
  local empty=$((width - filled))
  local percent=$((completed * 100 / total))
  local bar

  bar="$(printf '%*s' "$filled" '' | tr ' ' '#')"
  bar="$bar$(printf '%*s' "$empty" '')"
  printf '\r[%s] %3d%% %s' "$bar" "$percent" "$label"
}

print_step_header() {
  local step_name="$1"
  CURRENT_STEP=$((CURRENT_STEP + 1))
  draw_progress $((CURRENT_STEP - 1)) "$TOTAL_STEPS" "starting: $step_name"
  printf '\n'
  printf 'Step %d/%d: %s\n' "$CURRENT_STEP" "$TOTAL_STEPS" "$step_name"
}

print_step_done() {
  local step_name="$1"
  draw_progress "$CURRENT_STEP" "$TOTAL_STEPS" "done: $step_name"
  printf '\n'
}

run_step() {
  local step_name="$1"
  local log_file="$2"
  shift 2

  print_step_header "$step_name"
  if "$@" >"$log_file" 2>&1; then
    print_step_done "$step_name"
    printf '  log: %s\n' "${log_file#$REPO_ROOT/}"
  else
    local status=$?
    draw_progress $((CURRENT_STEP - 1)) "$TOTAL_STEPS" "failed: $step_name"
    printf '\n'
    printf 'Step failed: %s\n' "$step_name" >&2
    printf 'Full log: %s\n' "$log_file" >&2
    printf -- '--- log tail ---\n' >&2
    tail -n 40 "$log_file" >&2 || true
    exit "$status"
  fi
}

cd "$REPO_ROOT"

printf 'Fossil clean install\n'
printf 'Repo: %s\n' "$REPO_ROOT"
printf 'Logs: %s\n' "$SESSION_DIR"
printf 'Jobs: %s\n' "$MAKE_JOBS"
printf 'Install: %s%s\n' "${SUDO_BIN:+$SUDO_BIN }" "$INSTALL_CMD"
printf '\n'

if [[ -n "$SUDO_BIN" ]]; then
  INSTALL_RUNNER=(bash -lc "$SUDO_BIN $INSTALL_CMD")
else
  INSTALL_RUNNER=(bash -lc "$INSTALL_CMD")
fi

run_step "clean" \
  "$SESSION_DIR/01-clean.log" \
  make clean

run_step "configure" \
  "$SESSION_DIR/02-configure.log" \
  ./configure --json --with-zlib=tree

run_step "build" \
  "$SESSION_DIR/03-build.log" \
  make -j"$MAKE_JOBS"

run_step "install" \
  "$SESSION_DIR/04-install.log" \
  "${INSTALL_RUNNER[@]}"

printf '\nBuild complete.\n'
printf 'Logs written to %s\n' "$SESSION_DIR"
