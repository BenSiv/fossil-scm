# State Projection

Fossil's repository database is currently the canonical source of truth for the
exported domains in this command, but long-term durable textual knowledge need
not remain SQL-only.

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

- SQLite remains canonical for current exported domains and runtime metadata.
- Exported files are deterministic and overwrite-in-place.
- Exported state is grouped by user-meaningful domains, not by raw table dump.
- Sensitive settings are excluded from `config/settings.json`.
- Import is not implemented in this slice.

## Relationship To Knowledge Storage

This command is a projection layer, not a complete storage policy.

It should be read together with
[`doc/ai/STORAGE_MODEL.md`](ai/STORAGE_MODEL.md):

- runtime and retrieval metadata may remain SQL-native
- durable textual knowledge should increasingly be materialized as repository
  artifacts
- export is still useful for deterministic tree inspection and review

Projection should complement artifact-backed knowledge storage, not substitute
for it.

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
