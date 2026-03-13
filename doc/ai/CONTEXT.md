# Context Assembly

Defines how the agent builds its working context per task.

## Inputs
- Active files
- Last N micro-commits
- Relevant notes from the data pool, ranked by tier and metadata
- Steering and constitution

## Retrieval
- Start with metadata filters, provenance links, and tier-aware ranking.
- Prefer higher-tier notes when they answer the task cleanly.
- Pull lower-tier notes when the higher tiers do not provide enough coverage.
- Increase a note's future ranking when it is retrieved successfully.
- Increase the strength of note-to-note links when notes are retrieved together.
- Use strict caps per tier to avoid overload.

## Post-retrieval loop

After retrieval, evaluate the selected notes for:

- atomicity
- connectivity to other retrieved notes
- duplication and merge candidates
- title accuracy
- metadata updates based on processing level

## Output
- A single structured context payload with provenance links.
