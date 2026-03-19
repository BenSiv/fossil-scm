# Knowledge Storage Model

Purpose: define where AI-related knowledge should live over time, which
storage layers are canonical for which kinds of records, and how the web UI
should browse them.

## Problem

The current docs describe three partially overlapping storage models:

- SQLite-native pool tables such as `ai_note`, `ai_retrieval`, and
  `ai_chat_eval`
- Fossil-native artifacts such as wiki pages and technotes
- file-tree projection via `fossil state export DIRECTORY`

Without a clearer policy, the implementation can drift toward duplicated text,
unclear ownership, and UI surfaces that only expose one layer at a time.

## Direction

Fossil should treat **repository artifacts as the durable home for textual
knowledge** and **SQLite as the control plane, retrieval index, and runtime
audit layer**.

In concrete terms:

- durable textual knowledge should not remain only inside opaque SQL rows
- SQL should track metadata, relationships, retrieval, evals, and run history
- promoted or user-meaningful text should be materialized as repository
  artifacts that can be browsed, diffed, linked, and versioned
- the UI should browse both the indexed pool view and the artifact view

## Canonical Storage By Domain

### 1. Runtime and orchestration state: SQLite-first

These records are operational and should remain database-native:

- live chat sessions
- incremental event streams
- retrieval events and note-level retrieval results
- eval rows
- saved orchestration runs
- task status and background-job state

These are append-heavy, query-oriented, and often not useful as direct
repository documents.

### 2. Durable textual knowledge: artifact-first

These records should become repository-visible textual artifacts:

- curated notes
- promoted atomic concepts
- durable summaries
- rationale or decision records worth long-term reuse
- repository guidance
- skill manifests and orchestration guidance

These should be readable through ordinary Fossil artifact and document paths,
not only through SQL-backed custom pages.

### 3. Derived projection: export or mirror views

File-tree exports are useful, but they are not the primary source of truth.

They should exist for:

- inspection outside the UI
- diffing in ordinary file tools
- optional review workflows
- future import/sync experiments

But projection should follow canonical storage, not replace it.

## Tier Mapping

The existing tier model remains valid, but the storage policy should be more
explicit.

- Tier 0: raw captures may begin in SQLite-backed pool storage
- Tier 1: working/grouped notes may still be DB-backed while unstable
- Tier 2: draft syntheses should normally be materialized as repository text
- Tier 3: atomic concepts should normally be repository artifacts

Recommended mapping:

- Tier 0:
  - SQL `ai_note` rows
  - optional technotes tagged for raw capture when reviewability matters
- Tier 1:
  - SQL `ai_note` rows plus linked summaries
  - optional working documents in a draft namespace
- Tier 2:
  - draft wiki pages, technotes, or versioned files under a knowledge tree
- Tier 3:
  - stable wiki pages or versioned files representing durable concepts

## Repository Artifact Options

Fossil has more than one artifact class. The project should use whichever best
matches the artifact’s lifecycle.

### Wiki / technotes

Best for:

- durable concepts
- human-edited summaries
- reviewable rationale
- promoted knowledge objects

Advantages:

- already versioned and browseable in Fossil
- natural links and history
- good fit for promoted knowledge

### Versioned files in a repository tree

Best for:

- structured knowledge packs
- artifact sets intended for directory browsing
- machine-generated but reviewable text
- workflows that should look like ordinary repository content

Recommended target shape:

```text
knowledge/
  tier0/
  tier1/
  tier2/
  tier3/
  retrieval/
  runs/
  provenance/
```

This tree does not need to store every transient event, but it is the right
place for materialized textual artifacts that users should browse directly.

### SQL only

Best for:

- transient logs
- retrieval scoring data
- graph/link indexes
- execution metadata
- caches

SQL-only storage should be the exception for durable human-readable text, not
the default.

## UI Implications

The UI should expose **all knowledge elements**, not only chat sessions and a
small pool summary.

Required browsing surfaces:

- a knowledge browser over all indexed notes and artifacts
- tier filters
- source-type filters
- processing-level filters
- duplicate and merge lineage
- retrieval history per answer and per note
- artifact location or artifact reference for each durable note
- saved runs browser
- direct links from indexed notes to their materialized artifact

The UI should not force the user to know whether a record currently lives in a
table, a wiki page, a technote, or a versioned file.

## Metadata Responsibilities

Even when text is stored as an artifact, SQL metadata remains important.

SQLite should continue to hold:

- stable IDs
- artifact references
- provenance
- tier and processing level
- retrieval counters
- duplicate and merge links
- ranking signals
- eval summaries

This makes SQLite the query/index layer, not the sole text container.

### Explicit Artifact-Link Fields

The current schema can browse notes by provenance, but it does not yet model
durable artifact references cleanly enough.

Recommended additions for `ai_note`:

- `artifact_kind` TEXT
- `artifact_ref` TEXT
- `artifact_rid` INTEGER
- `artifact_path` TEXT
- `artifact_status` TEXT

These fields should describe the materialized durable artifact. They should not
replace:

- `source_type`
- `source_id`
- `source_ref`

Those existing fields continue to describe where the note came from.

## State Projection Relationship

`fossil state export DIRECTORY` should evolve toward a structured projection of
knowledge artifacts and related metadata, but it should not be the only path
to inspect durable text.

Longer term:

- exported tree layout should align with the `knowledge/` artifact structure
- projections should include stable references back to artifact IDs or paths
- selective import may later be added for controlled domains

## Non-Goals

This direction does not require:

- storing every transient prompt or event as a versioned file
- replacing Fossil’s DB-native strengths with a filesystem-only design
- duplicating every text field in every storage layer

The goal is a clear split:

- **SQLite for runtime, indexing, and audit**
- **repository artifacts for durable text**
- **UI over both**

## Immediate Implementation Consequences

1. Add a web knowledge browser that lists all indexed knowledge elements and
   links them to their artifact form when present.
2. Add explicit artifact-reference fields where missing from the AI metadata
   model.
3. Define one materialization path for Tier 2 and Tier 3 notes.
4. Treat `state export` as projection and interoperability, not as the only
   file-based storage path.

## Migration Rules

The migration from SQL-only durable text to artifact-backed durable text should
be incremental.

- existing `ai_note.body` content remains valid during migration
- artifact links are additive metadata, not destructive replacement
- Tier 2 and Tier 3 notes can be backfilled first
- Tier 0 and Tier 1 may remain SQL-first longer
- UI browsing must continue to work for notes that do not yet have an artifact
  link

This avoids a forced big-bang migration while still making the long-term
storage model explicit.
