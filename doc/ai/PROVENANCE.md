# Provenance Model

Goal: every retrieved or promoted knowledge artifact should remain attributable
to the sources and working notes that support it.

## Core Fields

- source type
- source identifier or route reference
- note ID
- retrieval event ID
- timestamps in UTC
- tier and processing level
- artifact kind and durable artifact reference
- review or promotion status

## Provenance Responsibilities

The system should be able to answer:

- where did this note come from
- what artifact or source does it point back to
- what retrieval or review actions involved it
- whether it has been merged, superseded, or promoted
- what durable artifact now represents it, if any

## Storage Model

- `ai_note` stores pooled notes and durable-artifact linkage metadata
- `ai_retrieval` and related tables store retrieval history
- review tables store promotion and quality decisions
- Fossil-native artifacts such as wiki pages or versioned files hold durable
  text once materialized

SQLite is the metadata and audit layer. Fossil artifacts are the durable home
for promoted text.

## UI Expectations

The UI should surface provenance instead of hiding it.

Required user-facing provenance affordances:

- source links from notes to original artifacts
- durable artifact links from promoted notes
- retrieval history for a note
- duplicate, merge, and supersession lineage
- review status that explains why promotion happened

## Policy

- provenance fields describe origin
- artifact fields describe durable publication targets
- these two concepts must not be conflated
- durable summaries should cite their supporting sources or note lineage
