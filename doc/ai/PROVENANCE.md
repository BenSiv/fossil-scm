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
- `/agent-chat` persists raw user prompts and raw model replies in `agentchat`.
- Visible model reasoning emitted in the normal reply channel is currently
  stored as reply text; there is no separate hidden chain-of-thought field.

## Privacy policy

- Sensitive records should be explicitly tagged in metadata.
- Stored reasoning should remain attributable to its source context.
- Reasoning artifacts may be persisted when they are useful to the
  repository knowledge pool and conform to project policy.
- Favor structured rationale, summaries, decision trails, and linked
  context over unbounded raw dumps.
- Stored reasoning should remain concise, declarative, and user-safe.
