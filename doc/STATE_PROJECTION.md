# State Projection

Fossil's repository database is the canonical source of truth.

`fossil state export DIRECTORY` provides a deterministic file-tree projection
of selected non-check-in repository state so that it can be inspected,
reviewed, and versioned outside the SQLite repository file.

## Current Scope

The current export is intentionally export-only.

It writes:

- `manifest.json`
- `config/settings.json`
- `config/reportfmt.json`
- `ai/notes.json`
- `ai/chat-eval.json`

## Design Rules

- SQLite remains canonical.
- Exported files are deterministic and overwrite-in-place.
- Exported state is grouped by user-meaningful domains, not by raw table dump.
- Sensitive settings are excluded from `config/settings.json`.
- Import is not implemented in this slice.

## Intended Use

- inspect repository-local state outside the Fossil UI
- review non-code state changes in ordinary file tools
- create a stable basis for later `state diff` or selective `state import`

## Non-Goals

This command does not attempt to mirror all Fossil internals.

It does not currently export:

- artifact internals
- sync state
- checkout-local state
- shun/admin/private operational tables
- wiki, ticket, or forum content

Those domains need explicit round-trip and ownership decisions before they
should be projected.
