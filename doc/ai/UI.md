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
- Browse all indexed knowledge elements, not only the current chat session
- Show artifact location or artifact reference for durable notes
- Expose duplicate and merge lineage
- Expose retrieval history per note and per answer
- Link indexed notes to their repository artifact form when materialized

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

## Storage Expectation

The UI should span both storage layers:

- SQLite-backed runtime and retrieval metadata
- repository artifacts holding durable textual knowledge

Users should not need to know which storage layer currently holds a given
knowledge element in order to browse it.
