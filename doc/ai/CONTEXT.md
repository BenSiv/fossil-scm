# Context Assembly

Defines how retrieval builds a working context payload for knowledge browsing,
review, and AI-assisted use.

## Inputs

- active request text
- relevant notes from the data pool, ranked by tier and metadata
- linked source artifacts
- steering and project guidance
- durable knowledge artifacts when available

## Retrieval

- start with metadata filters, provenance links, and tier-aware ranking
- prefer higher-tier notes when they answer the request cleanly
- pull lower-tier notes when higher tiers do not provide enough coverage
- increase a note's future ranking when it is retrieved successfully
- increase note-to-note linkage strength when notes are repeatedly retrieved
  together
- use strict caps per tier to avoid overload

## Post-Retrieval Loop

After retrieval, evaluate the selected notes for:

- atomicity
- connectivity to other retrieved notes
- duplication and merge candidates
- title accuracy
- metadata and promotion readiness

## Output

- a structured context payload with provenance links
- enough source visibility to justify later promotion or review decisions
