# Knowledge Tiers

Purpose: define the hierarchical documentation system from raw notes to atomic concepts.

## Tier 0: Fleeting
- Raw prompt traces, diffs, and transient scratch notes.
- Stored as `ai_note` with `tier=0` and optional technotes.

## Tier 1: Working
- Threaded clarifications and intermediate summaries.
- Stored as `ai_note` with `tier=1` or wiki pages under a `draft/` namespace.

## Tier 2: Draft
- Structured concepts that are not yet canonical.
- Stored as wiki pages under `draft/` with links to sources.

## Tier 3: Atomic
- Minimal, canonical concepts with durable value.
- Stored as main wiki pages.

## Promotion rules (summary)
- Promote on repeated retrieval, high similarity clustering, or frequent use in commits.
- Demote or sink only by decay of heat; do not delete by default.
