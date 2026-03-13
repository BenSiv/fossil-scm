# Web UI Surfaces

Primary interfaces exposed to the user.

## Chat
- Prompt input
- Agent response stored in `agentchat` with model and session metadata
- First-use web requests may create `agentchat_session` and `agentchat` lazily
- When the model emits visible reasoning text, it is currently stored as part of
  the normal agent reply payload
- Provider/backend identity should be surfaced explicitly next to the model
  selection, rather than inferred from a free-form model string
- `/agentui` should expose the effective config source and resolved backend/model
  pair for debugging

## Data Pool
- Note browser with tier, processing level, and source filters
- Retrieval count and related-note graph
- Semantic retrieval can use a separate embedding model from the chat model

## Wiki
- Atomic concepts with tier badges

## Change Log
- Micro-commit stream with rationale

## Analytics
- Knowledge density
- Concept velocity
- Alignment score

## Tasks
- Minimal task list tied to artifacts
