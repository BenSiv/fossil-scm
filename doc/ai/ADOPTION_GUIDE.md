# Open Knowledge Commons Adoption Guide

Purpose: describe how an outside team should evaluate and adopt this Fossil
fork as self-hosted knowledge infrastructure.

## Who Should Evaluate It

This project is a fit for teams that want:

- self-hosted collaboration infrastructure
- durable project memory with provenance
- a lightweight stack without a large external service dependency
- shared knowledge that can be reviewed, versioned, and exported

## Evaluation Path

### 1. Start with an existing Fossil workflow

The project assumes Fossil already acts as the home for repository history,
wiki, tickets, forum, and web UI.

### 2. Enable the knowledge layer on a small internal project

Use a contained project to validate:

- note capture and indexing
- browse and retrieval behavior
- promotion into draft and durable artifacts
- provenance and review surfaces

### 3. Decide on durable targets

Pick the artifact form that best matches your workflow:

- wiki pages for human-maintained concepts and summaries
- versioned files for structured knowledge trees and repository browsing

### 4. Define review habits

Agree on:

- who can promote draft notes
- how stale artifacts are marked
- how duplicate or superseded notes are handled
- what counts as a durable canonical concept

## Operating Model

The recommended operating model is local-first and small-team friendly:

- keep the repository self-hosted
- keep SQLite as the retrieval and metadata layer
- publish durable text as Fossil-native artifacts
- treat provenance and review as part of ordinary project maintenance

## Documentation Set

For adopters, the key documents are:

- `IMPLEMENTATION_PLAN.md`
- `USER_GUIDE.md`
- `DATA_POOL.md`
- `STORAGE_MODEL.md`
- `PROVENANCE.md`
- `SCHEMA.md`
- `TEST_PLAN.md`

## Adoption Outcome

Successful adoption means the team can:

- capture working knowledge without scattering it across tools
- browse and retrieve that knowledge with source visibility
- promote the most useful material into durable shared artifacts
- keep the whole system inspectable and portable
