# Micro‑Commit Policy

Micro‑commits capture incremental work with full provenance.

## Trigger
- Any agent‑initiated change to working files or wiki content.

## Required metadata
- Prompt
- Rationale (short)
- Model ID
- Related task or ticket

## Environment inputs
- `FOSSIL_AI_PROMPT`
- `FOSSIL_AI_RATIONALE`
- `FOSSIL_AI_MODEL`
- `FOSSIL_AI_TAGS`

## Guardrails
- Do not create micro‑commits for no‑op changes.
- Group changes if they are part of a single atomic action.
