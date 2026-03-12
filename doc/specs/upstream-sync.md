# Upstream Sync Strategy

This fork is intentionally divergent from `drhsqlite/fossil-mirror`, but it
still wants regular upstream intake. The correct strategy is not periodic
rebasing. It is a controlled merge workflow.

## Recommended strategy

- Track the official Fossil mirror as a dedicated `upstream` remote.
- Keep fork-specific work on `master`.
- Periodically merge `upstream/master` into `master`.
- Preserve explicit merge commits so upstream intake remains auditable.
- Automate known conflict resolution for recurring structural differences.

This is the right fit for this fork because:

- the fork has long-lived structural changes
- the fork has feature work that should keep its own history
- upstream updates should be incorporated, not used to rewrite fork history
- repeated conflict patterns should be encoded once and reused

## Why not rebase?

Rebasing a heavily divergent fork onto upstream repeatedly is expensive and
error-prone:

- it rewrites local history
- it makes conflict resolution harder to audit
- it forces the same structural conflicts to be revisited on many commits
- it is a poor fit for a fork that intentionally changes repository layout

Rebasing is acceptable only for short-lived topic branches. It should not be
the default intake mechanism for this fork's mainline.

## Automation

Use:

```sh
dev/tools/sync-upstream.sh
```

The script will:

- ensure the `upstream` remote exists and points to
  `https://github.com/drhsqlite/fossil-mirror.git`
- fetch `upstream/master`
- auto-stash a dirty worktree
- merge `upstream/master` into local `master`
- auto-resolve the known vendored zlib layout conflicts in favor of the fork's
  `dep/vendor/compat/zlib/` structure
- restore the stashed worktree

Useful options:

```sh
dev/tools/sync-upstream.sh --dry-run
dev/tools/sync-upstream.sh --push
dev/tools/sync-upstream.sh --no-stash
```

## Conflict policy

For the vendored zlib tree:

- keep the fork's directory placement under `dep/vendor/compat/zlib/`
- accept upstream content updates inside that directory
- keep upstream deletions for files that upstream removed
- preserve merge commits documenting when upstream was absorbed

If new conflict classes appear outside that known pattern, stop and resolve
them deliberately instead of broadening the automation blindly.
