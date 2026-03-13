# Provenance Model

Goal: every agent action is attributable to a prompt, a rationale, and the repository artifacts it touched.

## Core fields

- Prompt: exact user request or system trigger.
- Rationale or reasoning artifact reference.
- Model ID: provider and model name.
- Token usage: inbound/outbound counts (if available).
- Timestamps: UTC time of action.
- Artifact link: blob `rid` or wiki/technote identifier.
- Note metadata: tier, processing level, and retrieval counters.

## Storage

- `ai_context` is the canonical store.
- `ai_note` may reference `ai_context` for tiered notes.
- Data pool captures may include prompts, reasoning traces, documentation, wiki
  pages, and derived notes.

## Privacy policy

- Sensitive records should be explicitly tagged in metadata.
- Stored reasoning should remain attributable to its source context.
