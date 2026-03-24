# Open Knowledge Commons Implementation Plan

Purpose: define the implementation roadmap for the NGI Zero Open Knowledge
Commons work on this Fossil fork.

This plan replaces the earlier provider-split, cross-project orchestration, and
AgentOps roadmaps as the primary implementation document for `doc/ai/`.

## Scope

The project focus is now four concrete capabilities:

1. a knowledge browser inside the Fossil web UI
2. durable knowledge artifacts promoted from indexed notes
3. provenance-aware retrieval and review surfaces
4. deployability and documentation for self-hosted adopters

## Current Baseline

The repository already has useful foundations:

- note-oriented knowledge storage in `ai_note`
- tiered curation concepts from raw capture to durable concepts
- retrieval metadata and review-oriented tables
- web-facing AI routes and local-first deployment assumptions
- internal planning for browser views, storage mapping, and provenance

The current gap is not raw capability alone. The gap is packaging these pieces
into a coherent knowledge layer that outside users can browse, trust, and
adopt.

## Design Principles

- keep Fossil self-hosted and lightweight
- make durable knowledge browseable as Fossil-native artifacts
- keep SQLite as the metadata, retrieval, and audit layer
- treat provenance as a first-class user-facing feature
- prefer reviewable promotion over silent automatic publication
- document an adoption path for small teams and public-interest projects

## Milestone 1: Knowledge Browser

Objective: deliver a usable browse surface for indexed knowledge.

### Deliverables

- `/knowledge` summary page oriented around shared project memory
- `/knowledge-browser` listing indexed notes and knowledge objects
- filters for tier, source type, processing level, and artifact status
- text search across note title, body, and durable artifact references
- links back to wiki pages, technotes, tickets, docs, and repository artifacts
- review queues for promotion, stale artifacts, and duplicate cleanup

### Implementation Notes

- start on top of the existing `ai_note` schema
- expose retrieval count and recent retrieval history in row/card views
- keep first release simple and browse-focused rather than analytics-heavy

### Exit Criteria

- users can browse indexed knowledge through the web UI
- users can filter and inspect provenance without direct SQL access
- note pages or row actions clearly link back to source and artifact targets

## Milestone 2: Durable Artifact Layer

Objective: turn useful indexed notes into durable, reviewable project artifacts.

### Deliverables

- explicit artifact fields on `ai_note`
- promotion flow from indexed notes to durable artifacts
- support for draft, materialized, stale, and superseded states
- wiki-backed and file-backed materialization targets
- admin-facing relink and backfill operations

### Implementation Notes

- preserve `source_type`, `source_id`, and `source_ref` as provenance fields
- add separate durable artifact references instead of overloading provenance
- prefer draft wiki pages or a `knowledge/` tree for early materialization
- avoid bulk rewriting existing notes during the first migration

### Exit Criteria

- durable notes have an explicit artifact target
- users can promote and review knowledge instead of leaving it in SQL-only form
- stale or superseded artifacts are visible and auditable

## Milestone 3: Provenance And Retrieval Trust

Objective: make retrieval and AI-assisted use inspectable and easier to trust.

### Deliverables

- source-linked retrieval traces
- per-note provenance paths from derived text back to sources
- review records for promotion and retrieval quality
- UI affordances for duplicate, merge, and curation lineage
- observable reuse signals that justify promotion decisions

### Implementation Notes

- store enough metadata to reconstruct where a note came from
- keep retrieval history and review actions queryable in SQLite
- surface provenance in the UI instead of hiding it behind backend internals
- favor concise rationale summaries over opaque hidden reasoning stores

### Exit Criteria

- users can explain why a note or answer was retrieved
- promotion decisions cite reuse and provenance signals
- durable artifacts remain attributable to earlier working notes and sources

## Milestone 4: Deployability And Documentation

Objective: make the knowledge features understandable and adoptable by others.

### Deliverables

- user guide for knowledge capture, browsing, promotion, and review
- administrator guide for setup, upgrade, and maintenance
- documentation for self-hosted evaluation by small teams
- clear repository index of active docs and deprecated directions removed

### Implementation Notes

- document the minimum viable setup first
- describe local-first and small-team deployment patterns
- keep docs focused on the knowledge layer rather than a generic agent platform

### Exit Criteria

- a new adopter can understand what the project does and how to evaluate it
- the repository docs no longer point in conflicting strategic directions

## Out Of Scope For This Plan

The following are not the primary drivers of the current project framing:

- cross-project Goose/Fabric/OpenClaw comparison work
- provider-selection strategy as the central roadmap
- generic AgentOps positioning
- autonomous micro-commit automation as the headline workflow

These implementation details may still exist in code where useful, but they no
longer define the project narrative or documentation hierarchy.

## Document Map

The active companion documents are:

- `DATA_POOL.md`
- `TIERS.md`
- `STORAGE_MODEL.md`
- `PROVENANCE.md`
- `KNOWLEDGE_BROWSER_IMPLEMENTATION_PLAN.md`
- `SCHEMA.md`
- `TEST_PLAN.md`
- `USER_GUIDE.md`
- `ADOPTION_GUIDE.md`

## Success Condition

At the end of this roadmap, Fossil should offer a practical self-hosted
knowledge layer: one that lets communities capture working knowledge, browse
it, promote the most useful parts into durable artifacts, and inspect the
provenance behind retrieval and AI-assisted use.
