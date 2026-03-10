# Provenance Model

Goal: every agent action is attributable to a prompt, a rationale, and the repository artifacts it touched.

## Core fields

- Prompt: exact user request or system trigger.
- Rationale: short justification aligned to constitution and steering rules.
- Model ID: provider and model name.
- Token usage: inbound/outbound counts (if available).
- Timestamps: UTC time of action.
- Artifact link: blob `rid` or wiki/technote identifier.

## Storage

- `ai_context` is the canonical store.
- `ai_note` may reference `ai_context` for tiered notes.

## Privacy policy

- No raw chain-of-thought is persisted.
- Rationale is concise, declarative, and user‑safe.
