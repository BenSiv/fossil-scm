# AI Feature Test Plan

## Schema
- Create tables on new repo
- No-op on existing repo with tables
- Create retrieval, review, and note-link tables without touching core Fossil schema

## Provenance
- Insert context rows
- Link to artifacts
- Persist prompts and reasoning traces as attributable data pool records
- Verify `/agent-chat` stores raw user prompts and raw model replies in `agentchat`
- Verify visible reasoning text is stored as part of the reply payload when emitted

## Tiers
- Promote and sink notes
- Heat decay behavior
- Higher tiers receive higher base retrieval weight
- Retrieval count raises future retrieval likelihood

## Evaluation loop
- Detect non-atomic notes after retrieval
- Record co-retrieval links between notes
- Detect exact duplicates and preserve merge backlinks
- Retitle only when the current title is inaccurate
- Update processing-level metadata after review

## UI
- Pages render
- Queries return expected results
- First-use `/agentui` and `/agent-chat` requests succeed without precreated
  agent chat tables or sessions
- Chat model and embedding model may be configured independently
- `/agentui` starts on a new chat unless a specific prior session is requested
- The displayed model reflects the effective resolved config or the explicitly
  opened session, not stale history
- Provider/model mismatches are detected before backend invocation once
  provider-aware validation is added
