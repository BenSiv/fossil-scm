#!/usr/bin/env bash

set -euo pipefail

MODE="update"
GIT_REPO=""
REPO_ROOT=""
FOSSIL_BIN="${FOSSIL_BIN:-fossil}"
FOSSIL_REPO=""
GIT_MARKS=""
FOSSIL_MARKS=""
ADMIN_USER="${USER:-$(id -un)}"
USE_AUTHOR=0
RUN_REBUILD=1
QUIET=0
FORCE=0
NO_VACUUM=0

attribute_args=()

usage() {
  cat <<'EOF'
Usage: dev/tools/git-to-fossil.sh [options]

Create or update a Fossil repository from the current Git repository state.

Modes:
  update   Incrementally import new Git history into an existing Fossil repo.
           If the target repo does not exist yet, it is created.
  rebuild  Recreate the Fossil repo from scratch using the current Git history.

Options:
  --mode MODE          update or rebuild (default: update)
  --git-repo PATH      Source Git repository path
                       (default: current Git worktree)
  --repo PATH          Fossil repository path
                       (default: scm/git-export.fossil)
  --git-marks PATH     Git fast-export marks path
                       (default: scm/git-export.git.marks)
  --fossil-marks PATH  Fossil import marks path
                       (default: scm/git-export.fossil.marks)
  --admin-user NAME    Admin user for fresh repo creation
                       (default: current user)
  --use-author         Use Git author instead of committer during import
  --attribute MAP      Pass through a Fossil import attribution mapping
                       Example: --attribute "dev@example.com drh"
  --no-post-rebuild    Skip 'fossil rebuild --ifneeded' after import
  --no-vacuum          Pass --no-vacuum to fossil import
  --quiet              Reduce tool output
  --force              In rebuild mode, replace the target repo without backup
  -h, --help           Show this help

Environment:
  FOSSIL_BIN           Fossil executable to use (default: fossil)
EOF
}

log() {
  if [[ "$QUIET" -eq 0 ]]; then
    printf '[git-to-fossil] %s\n' "$*"
  fi
}

die() {
  printf '[git-to-fossil] error: %s\n' "$*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

run_fossil() {
  USER="$ADMIN_USER" "$FOSSIL_BIN" "$@"
}

abs_path() {
  local path="$1"
  if [[ "$path" = /* ]]; then
    printf '%s\n' "$path"
  else
    printf '%s/%s\n' "$REPO_ROOT" "$path"
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    update|rebuild)
      MODE="$1"
      shift
      ;;
    --mode)
      [[ $# -ge 2 ]] || die "--mode requires an argument"
      MODE="$2"
      shift 2
      ;;
    --git-repo)
      [[ $# -ge 2 ]] || die "--git-repo requires an argument"
      GIT_REPO="$2"
      shift 2
      ;;
    --repo)
      [[ $# -ge 2 ]] || die "--repo requires an argument"
      FOSSIL_REPO="$2"
      shift 2
      ;;
    --git-marks)
      [[ $# -ge 2 ]] || die "--git-marks requires an argument"
      GIT_MARKS="$2"
      shift 2
      ;;
    --fossil-marks)
      [[ $# -ge 2 ]] || die "--fossil-marks requires an argument"
      FOSSIL_MARKS="$2"
      shift 2
      ;;
    --admin-user)
      [[ $# -ge 2 ]] || die "--admin-user requires an argument"
      ADMIN_USER="$2"
      shift 2
      ;;
    --attribute)
      [[ $# -ge 2 ]] || die "--attribute requires an argument"
      attribute_args+=("--attribute" "$2")
      shift 2
      ;;
    --use-author)
      USE_AUTHOR=1
      shift
      ;;
    --no-post-rebuild)
      RUN_REBUILD=0
      shift
      ;;
    --no-vacuum)
      NO_VACUUM=1
      shift
      ;;
    --quiet)
      QUIET=1
      shift
      ;;
    --force)
      FORCE=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

case "$MODE" in
  update|rebuild) ;;
  *) die "invalid mode: $MODE" ;;
esac

require_cmd git
require_cmd "$FOSSIL_BIN"

if [[ -n "$GIT_REPO" ]]; then
  REPO_ROOT="$(abs_path "$GIT_REPO")"
else
  REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
fi

git -C "$REPO_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1 \
  || die "not inside a git work tree: $REPO_ROOT"

if [[ -z "$FOSSIL_REPO" ]]; then
  FOSSIL_REPO="$REPO_ROOT/scm/git-export.fossil"
else
  FOSSIL_REPO="$(abs_path "$FOSSIL_REPO")"
fi
if [[ -z "$GIT_MARKS" ]]; then
  GIT_MARKS="$REPO_ROOT/scm/git-export.git.marks"
else
  GIT_MARKS="$(abs_path "$GIT_MARKS")"
fi
if [[ -z "$FOSSIL_MARKS" ]]; then
  FOSSIL_MARKS="$REPO_ROOT/scm/git-export.fossil.marks"
else
  FOSSIL_MARKS="$(abs_path "$FOSSIL_MARKS")"
fi

mkdir -p "$(dirname "$FOSSIL_REPO")" "$(dirname "$GIT_MARKS")" "$(dirname "$FOSSIL_MARKS")"

run_full_import() {
  local target_repo="$1"
  local git_args=("--all" "--export-marks=$GIT_MARKS")
  local fossil_args=("import" "--git" "--admin-user" "$ADMIN_USER" "--export-marks" "$FOSSIL_MARKS")

  if [[ "$USE_AUTHOR" -eq 1 ]]; then
    fossil_args+=("--use-author")
  fi
  if [[ "$NO_VACUUM" -eq 1 ]]; then
    fossil_args+=("--no-vacuum")
  fi
  if [[ "${#attribute_args[@]}" -gt 0 ]]; then
    fossil_args+=("${attribute_args[@]}")
  fi
  if [[ "$QUIET" -eq 1 ]]; then
    fossil_args+=("-q")
  fi

  log "creating Fossil repository at $target_repo from Git history"
  git -C "$REPO_ROOT" fast-export "${git_args[@]}" \
    | run_fossil "${fossil_args[@]}" "$target_repo"
}

run_incremental_import() {
  local git_args=("--all" "--export-marks=$GIT_MARKS")
  local fossil_args=("import" "--git" "--incremental" "--export-marks" "$FOSSIL_MARKS")

  [[ -f "$GIT_MARKS" ]] && git_args=("--import-marks=$GIT_MARKS" "${git_args[@]}")
  [[ -f "$FOSSIL_MARKS" ]] && fossil_args+=("--import-marks" "$FOSSIL_MARKS")

  if [[ "$USE_AUTHOR" -eq 1 ]]; then
    fossil_args+=("--use-author")
  fi
  if [[ "$NO_VACUUM" -eq 1 ]]; then
    fossil_args+=("--no-vacuum")
  fi
  if [[ "${#attribute_args[@]}" -gt 0 ]]; then
    fossil_args+=("${attribute_args[@]}")
  fi
  if [[ "$QUIET" -eq 1 ]]; then
    fossil_args+=("-q")
  fi

  log "incrementally updating Fossil repository at $FOSSIL_REPO"
  git -C "$REPO_ROOT" fast-export "${git_args[@]}" \
    | run_fossil "${fossil_args[@]}" "$FOSSIL_REPO"
}

post_rebuild() {
  [[ "$RUN_REBUILD" -eq 1 ]] || return 0
  log "running fossil rebuild --ifneeded"
  if [[ "$QUIET" -eq 1 ]]; then
    run_fossil rebuild "$FOSSIL_REPO" --ifneeded --quiet
  else
    run_fossil rebuild "$FOSSIL_REPO" --ifneeded
  fi
}

rebuild_repo() {
  local tmp_repo="$FOSSIL_REPO.tmp.$$"
  local backup_repo="$FOSSIL_REPO.bak.$(date +%Y%m%d-%H%M%S)"

  rm -f "$tmp_repo"
  rm -f "$GIT_MARKS" "$FOSSIL_MARKS"
  run_full_import "$tmp_repo"

  if [[ -f "$FOSSIL_REPO" ]]; then
    if [[ "$FORCE" -eq 1 ]]; then
      log "replacing existing repository at $FOSSIL_REPO"
      rm -f "$FOSSIL_REPO"
    else
      log "backing up existing repository to $backup_repo"
      mv "$FOSSIL_REPO" "$backup_repo"
    fi
  fi

  mv "$tmp_repo" "$FOSSIL_REPO"
}

if [[ "$MODE" = "rebuild" ]]; then
  rebuild_repo
else
  if [[ -f "$FOSSIL_REPO" ]]; then
    run_incremental_import
  else
    rm -f "$GIT_MARKS" "$FOSSIL_MARKS"
    run_full_import "$FOSSIL_REPO"
  fi
fi

post_rebuild
log "done"
