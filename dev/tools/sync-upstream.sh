#!/usr/bin/env bash

set -euo pipefail

UPSTREAM_REMOTE="${UPSTREAM_REMOTE:-upstream}"
UPSTREAM_URL="${UPSTREAM_URL:-https://github.com/drhsqlite/fossil-mirror.git}"
UPSTREAM_BRANCH="${UPSTREAM_BRANCH:-master}"
TARGET_BRANCH="${TARGET_BRANCH:-master}"
AUTO_STASH=1
AUTO_PUSH=0
DRY_RUN=0

usage() {
  cat <<'EOF'
Usage: dev/tools/sync-upstream.sh [options]

Merge the latest upstream Fossil changes into this divergent fork while
preserving the fork's repository layout and resolving known vendored zlib
path conflicts automatically.

Options:
  --branch NAME         Target local branch to update (default: master)
  --upstream-branch N   Upstream branch to merge (default: master)
  --remote NAME         Upstream remote name (default: upstream)
  --url URL             Upstream remote URL
  --no-stash            Fail instead of auto-stashing local changes
  --push                Push the updated target branch to origin after merge
  --dry-run             Fetch only and print what would be merged
  -h, --help            Show this help
EOF
}

log() {
  printf '[sync-upstream] %s\n' "$*"
}

die() {
  printf '[sync-upstream] error: %s\n' "$*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --branch)
      TARGET_BRANCH="$2"
      shift 2
      ;;
    --upstream-branch)
      UPSTREAM_BRANCH="$2"
      shift 2
      ;;
    --remote)
      UPSTREAM_REMOTE="$2"
      shift 2
      ;;
    --url)
      UPSTREAM_URL="$2"
      shift 2
      ;;
    --no-stash)
      AUTO_STASH=0
      shift
      ;;
    --push)
      AUTO_PUSH=1
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done

git rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "not inside a git work tree"
[[ -z "$(git rev-parse -q --verify MERGE_HEAD 2>/dev/null || true)" ]] || die "merge already in progress"

current_branch="$(git branch --show-current)"
[[ -n "$current_branch" ]] || die "detached HEAD is not supported"

stash_name=""

restore_stash() {
  if [[ -n "$stash_name" ]]; then
    log "restoring stashed worktree changes"
    git stash pop --index >/dev/null || die "stash restore failed; recover it manually with 'git stash list'"
  fi
}

trap restore_stash EXIT

ensure_remote() {
  if git remote get-url "$UPSTREAM_REMOTE" >/dev/null 2>&1; then
    existing_url="$(git remote get-url "$UPSTREAM_REMOTE")"
    if [[ "$existing_url" != "$UPSTREAM_URL" ]]; then
      log "updating $UPSTREAM_REMOTE remote URL"
      git remote set-url "$UPSTREAM_REMOTE" "$UPSTREAM_URL"
    fi
  else
    log "adding $UPSTREAM_REMOTE remote"
    git remote add "$UPSTREAM_REMOTE" "$UPSTREAM_URL"
  fi
}

auto_stash_if_needed() {
  if git diff --quiet && git diff --cached --quiet; then
    return
  fi
  if [[ "$AUTO_STASH" -eq 0 ]]; then
    die "worktree is dirty; commit/stash changes or rerun without --no-stash"
  fi
  stash_name="sync-upstream-$(date +%Y%m%d-%H%M%S)"
  log "stashing local changes as $stash_name"
  git stash push --include-untracked -m "$stash_name" >/dev/null
}

resolve_known_conflicts() {
  local unresolved
  mapfile -t unresolved < <(git diff --name-only --diff-filter=U)
  [[ "${#unresolved[@]}" -gt 0 ]] || return 0

  log "resolving known vendored-layout conflicts"

  for f in "${unresolved[@]}"; do
    case "$f" in
      dep/vendor/compat/zlib/zconf.h)
        git checkout --theirs -- "$f"
        git add -f -- "$f"
        ;;
      dep/vendor/compat/zlib/os400/zlib.inc|compat/zlib/os400/zlib.inc)
        git rm --quiet -- "$f" || true
        ;;
      dep/vendor/compat/zlib/zconf.h.cmakein|dep/vendor/compat/zlib/contrib/untgz/Makefile|dep/vendor/compat/zlib/contrib/untgz/Makefile.msc|dep/vendor/compat/zlib/contrib/untgz/untgz.c|dep/vendor/compat/zlib/contrib/vstudio/*|dep/vendor/compat/zlib/nintendods/*|dep/vendor/compat/zlib/old/*)
        git rm --quiet -- "$f" || true
        ;;
      dep/vendor/compat/zlib/*)
        git checkout --theirs -- "$f"
        git add -f -- "$f"
        ;;
      *)
        die "unrecognized conflict path: $f"
        ;;
    esac
  done

  if [[ -n "$(git diff --name-only --diff-filter=U)" ]]; then
    die "unresolved conflicts remain after automatic resolution"
  fi
}

ensure_remote

if [[ "$current_branch" != "$TARGET_BRANCH" ]]; then
  log "checking out $TARGET_BRANCH"
  git checkout "$TARGET_BRANCH"
fi

auto_stash_if_needed

log "fetching $UPSTREAM_REMOTE/$UPSTREAM_BRANCH"
git fetch "$UPSTREAM_REMOTE" "$UPSTREAM_BRANCH"

ahead_behind="$(git rev-list --left-right --count "${TARGET_BRANCH}...${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}")"
log "divergence ${TARGET_BRANCH}...${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}: $ahead_behind"

if [[ "$DRY_RUN" -eq 1 ]]; then
  log "dry run requested; not merging"
  exit 0
fi

if git merge-base --is-ancestor "${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}" "$TARGET_BRANCH"; then
  log "target branch already contains ${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}"
  exit 0
fi

log "merging ${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH} into ${TARGET_BRANCH}"
if ! git merge --no-ff "${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}"; then
  resolve_known_conflicts
  git commit --no-edit
fi

if [[ "$AUTO_PUSH" -eq 1 ]]; then
  log "pushing ${TARGET_BRANCH} to origin"
  git push origin "$TARGET_BRANCH"
fi

log "upstream sync complete"
