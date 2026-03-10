# Context Assembly

Defines how the agent builds its working context per task.

## Inputs
- Active files
- Last N micro‑commits
- Relevant atomic concepts (tier 3)
- Steering and constitution

## Retrieval
- Prefer semantic search over full history.
- Use strict caps per tier to avoid overload.

## Output
- A single structured context payload with provenance links.
