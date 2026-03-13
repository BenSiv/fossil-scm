# Knowledge Tiers

Purpose: define the hierarchical note system from raw captures to atomic notes.

## Tier 0: Fleeting
- Raw prompts, chain-of-thought, imported documents, wiki captures, diffs, and
  transient scratch notes.
- Stored as `ai_note` with `tier=0`.

## Tier 1: Working
- Lightly processed working notes, grouped retrieval sessions, and intermediate
  summaries.
- Stored as `ai_note` with `tier=1` or wiki pages under a `draft/` namespace.

## Tier 2: Draft
- Structured concepts that are not yet canonical.
- Stored as wiki pages under `draft/` with links to sources.

## Tier 3: Atomic
- Single-subject, canonical notes with durable value.
- Stored as main wiki pages.

## Retrieval rules

- Tier defines the base likelihood of retrieval.
- Higher tiers should be preferred when they satisfy the request.
- Lower tiers remain available for provenance and gap-filling.

## Promotion rules (summary)
- Promote on repeated retrieval, high similarity clustering, or frequent use in commits.
- Increase future retrieval likelihood each time a note is retrieved.
- Demote or sink only by decay of heat; do not delete by default.
