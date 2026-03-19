# Knowledge Browser Implementation Plan

Purpose: turn the storage policy in [`STORAGE_MODEL.md`](STORAGE_MODEL.md)
into an implementation sequence for the web UI and metadata model.

## Goals

1. Let users browse all indexed knowledge elements through the web UI.
2. Make durable textual knowledge reachable as repository artifacts, not only
   SQL rows.
3. Keep SQLite as the retrieval, provenance, and audit layer.

## Principles

- Keep `/knowledge` as the dashboard and add a dedicated browse surface.
- Do not block browsing on the full artifact-materialization redesign.
- Treat artifact links as best-effort first, then replace them with explicit
  schema-backed references.
- Prefer narrow, reviewable steps over a single storage migration.

## Phase 1: Browser UI Over Existing Tables

Add a web page that lists `ai_note` rows directly and supports:

- tier filter
- source-type filter
- processing-level filter
- text search over title, body, and source reference
- duplicate and merge lineage
- retrieval count and recent retrieval history
- best-effort links to wiki, ticket, doc, or technote sources

This phase can ship without schema changes because `ai_note` already stores:

- `tier`
- `title`
- `body`
- `source_type`
- `source_id`
- `source_ref`
- `process_level`
- `retrieval_count`
- duplicate and merge references

Limitation in this phase:

- artifact links are heuristic because `source_ref` is overloaded and there is
  no dedicated artifact reference column

## Phase 2: Explicit Artifact References

Extend `ai_note` metadata so durable notes can point to their materialized
artifact form without guessing.

Recommended additions:

- `artifact_kind` TEXT
  Values such as `wiki`, `technote`, `doc`, `file`, `artifact`.
- `artifact_ref` TEXT
  Stable route-oriented reference such as wiki page name, technote name, repo
  path, or artifact UUID.
- `artifact_rid` INTEGER
  Blob RID when the durable artifact has a direct repository artifact ID.
- `artifact_path` TEXT
  Versioned path when the durable note is materialized as a repository file.
- `artifact_status` TEXT
  Values such as `none`, `draft`, `materialized`, `stale`, `superseded`.

Migration rule:

- existing `source_type` and `source_ref` stay intact as provenance
- new artifact fields describe the durable materialization target
- provenance and artifact references must not be conflated

## Phase 3: Materialization Targets

Define one supported materialization path for Tier 2 and Tier 3 notes.

Recommended first target:

- Tier 2 draft syntheses:
  - wiki pages in a draft namespace such as `draft/...`
- Tier 3 durable concepts:
  - stable wiki pages or versioned files under `knowledge/tier3/`

Optional parallel target:

- versioned files under:

```text
knowledge/
  tier2/
  tier3/
  retrieval/
  provenance/
```

Selection rule:

- if the artifact is primarily human-authored and link-heavy, use wiki
- if the artifact is pack-oriented, machine-generated, or directory-browse
  oriented, use versioned files

## Phase 4: Browser Unification

After explicit artifact references exist, the browser should expose both the
indexed view and the artifact view as one system.

Each note row should show:

- indexed metadata
- materialization state
- direct artifact link
- provenance link
- retrieval history
- duplicate and merge lineage

Additional browse surfaces:

- saved runs browser
- retrieval browser
- promotion queue
- duplicate-cleanup queue

## Phase 5: Controlled Promotion and Backfill

Add commands and UI actions that materialize notes intentionally.

Recommended operations:

- promote note to draft artifact
- promote draft to durable artifact
- relink note to existing artifact
- mark artifact stale when note text diverges

Backfill rules:

- existing Tier 2 and Tier 3 notes without artifact links remain browseable
- backfill can be incremental and user-driven
- do not rewrite or delete note text during initial migration

## Processing Loop Promotion

Promotion should not depend only on explicit user commands.

The retrieval review loop should be allowed to advance notes upward when usage
signals justify it. Recommended signals:

- accumulated `retrieval_count`
- reinforced `heat`
- duplicate status
- atomicity review outcome

Recommended policy:

- duplicate or merge-target notes do not auto-promote
- notes that still need splitting do not auto-promote into Tier 3
- Tier 2+ notes without a durable artifact may receive `artifact_status=draft`
  automatically until materialization occurs
- review summaries should record promotion decisions explicitly so the loop is
  observable

## Proposed UI Sequence

1. `/knowledge` remains summary-first.
2. `/knowledge-browser` becomes the primary browse surface for indexed notes.
3. `/knowledge-runs` exposes saved orchestration runs in the web UI.
4. `/knowledge-retrievals` exposes retrieval history directly.
5. Per-note pages can come later if row cards become too dense.

## Proposed Schema Sequence

1. Ship browser over current `ai_note` schema.
2. Add artifact-reference columns.
3. Add materialization commands or UI actions.
4. Backfill artifact links for durable notes.
5. Add queue views for duplicate cleanup and promotion.

## Open Decisions

- Prefer wiki-first or file-tree-first for Tier 3.
- Whether Tier 0 raw captures should ever materialize by default.
- Whether `artifact_ref` should be route-oriented or canonical-ID-oriented.
- Whether durable knowledge files should live in the checkout tree, wiki, or
  both depending on note class.

## Acceptance Criteria

1. Users can browse all indexed notes through the web UI.
2. Users can filter by tier, source, and processing state.
3. Users can inspect retrieval history for a note without leaving the browser.
4. Durable notes can link to a repository artifact explicitly.
5. SQL remains the retrieval/provenance layer rather than the only text store.
