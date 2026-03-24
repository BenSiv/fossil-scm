# Open Knowledge Commons User Guide

Purpose: explain how to use the knowledge-layer features in this Fossil fork.

## What This Project Adds

Open Knowledge Commons extends Fossil with a shared knowledge layer built around:

- indexed notes and working knowledge
- tiered curation from raw capture to durable concepts
- promotion into reviewable wiki or file-backed artifacts
- provenance-aware retrieval and browse surfaces

The project is aimed at teams that want to keep documentation, decisions,
references, and retrieval workflows under local control.

## Core Workflow

### 1. Capture knowledge

Use notes and source-linked records to capture:

- working summaries
- technical decisions
- references and source excerpts
- draft syntheses
- project-specific guidance worth retrieving later

Early captures may remain in the indexed pool while they are still unstable.

### 2. Index and retrieve

The system indexes notes and related artifacts so they can be:

- searched
- filtered by tier and source type
- retrieved with provenance
- reviewed for later promotion

Higher-tier notes should become easier to reuse, while lower-tier captures stay
available for provenance and backfill.

### 3. Promote useful knowledge

When an indexed note proves useful through review or repeated reuse, promote it
into a durable artifact:

- a draft wiki page
- a stable wiki page
- a versioned file under a knowledge tree

Promotion should be explicit and reviewable. Provenance should remain attached
to the promoted artifact.

### 4. Review provenance

When browsing or retrieving knowledge, inspect:

- where the note came from
- what sources support it
- whether it has been promoted, superseded, or marked stale
- how often it has been reused

This is the trust model for the project: durable knowledge stays inspectable.

## Knowledge Tiers

- Tier 0: raw captures and imports
- Tier 1: working notes and grouped summaries
- Tier 2: draft syntheses ready for review
- Tier 3: durable concepts and canonical references

The tiers are a curation model, not a permission model. Lower tiers remain
valuable for provenance even when they are not the preferred retrieval target.

## Browser Views

The intended browser surfaces are:

- `/knowledge` for summary and queues
- `/knowledge-browser` for indexed note browsing
- artifact links back to wiki, technote, doc, ticket, or repository content
- provenance and retrieval views for review workflows

Exact route coverage depends on the implementation state of the current branch.

## Durable Artifacts

Durable knowledge should not live only in SQL rows.

Preferred durable targets:

- wiki pages for human-maintained concepts and summaries
- versioned files for structured knowledge packs and browseable trees

Draft and durable states should be visible from the browser so users can tell
which knowledge is still working material and which has been published into a
stable artifact.

## Who This Is For

This project is aimed at:

- software teams
- research collaborations
- standards and civic technology groups
- non-profits and small institutions
- communities that want self-hosted and portable knowledge infrastructure

## Working Assumption

AI-assisted retrieval can help surface relevant material, but durable changes
should remain reviewable by people. The project is therefore a knowledge system
with retrieval support, not a fully autonomous agent platform.
