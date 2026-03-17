# Pool Processing Loop Implementation Plan

This document outlines the implementation plan for the AI agent orchestration layer and the new self-maintaining knowledge pool processing loop.
provider-aware system without discarding the simplicity of local commands and
single-binary deployment.

## Current Status

Fossil has working foundations but is still short of the intended design.

Implemented now:

- split chat model and embedding model
- runtime config precedence with regression coverage
- `/agentui` diagnostics for effective config source, command, provider
  inference, and model values
- wrapper-based backends for Ollama and Codex
- Tcl regression coverage for agent CLI and first-use web flows

Still missing:

- first-class provider fields
- provider-aware validation
- provider/model discovery for the UI
- streaming chat transport
- structured message/event persistence
- conversation-level evaluation of chat quality

The practical state is:

- operationally usable for local testing
- not yet architecturally provider-aware

## Design Goals

- make runtime config and backend choice explicit
- remove backend/model ambiguity
- preserve simple local integrations
- make `/agentui` truthful about what backend will run
- prepare for streaming and structured message handling
- keep the current retrieval loop while extending evaluation to chat
- **Introduce TH1 Glue Layer**: Move orchestration, prompt engineering, and parsing from C to TH1 scripts for flexibility and safety. See [TH1_ORCHESTRATION.md](file:///wsl.localhost/Ubuntu/home/bensiv/fossil-scm/doc/ai/TH1_ORCHESTRATION.md).
- keep the core C logic lean and focused on "Muscle" (Vector search, DB access, process execution).

## Phase A: Visibility And Safety

Objective:

- make backend resolution inspectable
- eliminate silent ambiguity before deeper refactors

Delivered already:

- effective config diagnostics in `/agentui`
- config source reporting
- provider inference for diagnostics
- user config fallback and precedence tests

Remaining work in this phase:

- surface the effective config summary through a machine-readable endpoint, not
  only HTML
- expose whether the effective values came from explicit config or fallback
- add tests for empty-model and missing-command paths across CLI and web

Exit criteria:

- a developer can tell, from the UI or one command, exactly which config file,
  provider, command, and model are active
- obvious misconfiguration fails early and predictably

## Phase B: Provider Fields And Validation

Objective:

- make provider identity first-class instead of inferred from command strings

Implementation:

- add support for:
  - `provider`
  - `embedding_provider`
- keep these legacy fields working:
  - `command`
  - `embedding_command`
  - `model`
  - `embedding_model`
- if provider fields are absent, infer them for compatibility

Validation rules:

- reject `provider=ollama, model=auto`
- reject `provider=codex, model=qwen3.5:0.8b`
- reject embedding requests against providers/models known not to support
  embeddings when that can be determined early
- reject missing command/binary when a provider requires an external wrapper

Deliverables:

- updated config schema docs
- compatibility logic for old configs
- early provider/model mismatch errors
- Tcl tests for accepted and rejected combinations

Exit criteria:

- no backend choice depends on guessing from a free-form model string
- user-visible errors occur before launching the backend process

## Phase C: Provider-Aware UI And Session State

Objective:

- make `/agentui` operate on explicit backend identity, not implicit text entry

Implementation:

- replace the plain free-text chat model control with:
  - provider selector plus provider-scoped model field, or
  - provider selector plus model dropdown when discovery is available
- display the active provider and model for both chat and embeddings
- store provider identity alongside each chat session and message

Optional server additions:

- endpoint for effective provider/model/config data
- endpoint for provider capabilities and known models

Deliverables:

- no stale model field on new chat
- existing sessions reopen with the original provider/model pair
- no confusion between UI text and actual backend

Exit criteria:

- session metadata remains meaningful even after config changes
- the UI cannot silently display one backend while invoking another

---

### [pool-processing]

This component introduces the "pool processing loop" which allows the AI agent to self-maintain the knowledge pool, transitioning items between tiers (Atomic → Composed → Wiki → Curated → Derived).

#### [MODIFY] [agent.c](file:///wsl.localhost/Ubuntu/home/bensiv/fossil-scm/src/agent.c)
- **`ai_cmd` (CLI entry point)**: Add a `fossil agent pool-process <tier>` subcommand. This will invoke a TH1 script (either built-in or from the database config) that queries pending items for a tier and processes them.
- **`agent_register_th1`**: Register the 4 new TH1 pool commands described below.
- **TH1 Command Implementations**:
  - `pool_list_pending <tier_num>`: Returns a TCL list of `nid`s from `ai_note` that are ready to be processed *up* to the requested tier. E.g., if passing `2`, it queries notes at `tier=1` that haven't been processed yet.
  - `pool_get <nid>`: Returns the raw text/body of the specified note from `ai_note`.
  - `pool_put <target_tier_num> <body> <?metadata?>`: Creates a new `ai_note` at the target tier containing the synthesized body. Returns the new `nid`. Uses `ai_note_create`.
  - `pool_link <from_nid> <to_nid> <link_type>`: Records a relationship in `ai_note_link`. Uses `ai_note_link_upsert`. Uses link_type strings like `derived-from` or `composed-of`.

#### [MODIFY] [ai.c](file:///wsl.localhost/Ubuntu/home/bensiv/fossil-scm/src/ai.c)
- Expose `ai_note_link_upsert` for use by the TH1 layer by dropping the `static` keyword and adding it to `ai.h`.

---

## Phase D: Streaming And Structured Events

Objective:

- move from buffered one-shot replies to streamed, typed output
- replace hardcoded C chat logic with a flexible TH1 orchestration layer

Transport options:

- server-sent events
- chunked HTTP

Required backend behavior:

- incremental read of child process output
- progressive delivery to the UI
- final persistence after completion or failure

Introduce structured event/message types:

- `progress`
- `reasoning_visible`
- `tool_activity`
- `final_text`
- `error`

Storage options:

- extend `agentchat` with typed rows, or
- add a separate event table keyed to a chat message/session

Deliverables:

- live output in `/agentui`
- separation between visible reasoning and final answer
- future hooks for filtering, review, or summarization

Exit criteria:

- long-running chats show progress in real time
- Fossil no longer has to treat every backend response as one undifferentiated
  text blob

## Phase E: Conversation Evaluation Loop

Objective:

- extend evaluation from retrieval maintenance to answer-quality review

Keep the existing retrieval loop:

- retrieval reinforcement
- co-retrieval links
- duplicate detection
- title correction
- metadata normalization

Add later, after provider/model clarity and structured events exist:

- reply quality review records
- output classification
- explicit handling of visible reasoning text
- provider-specific post-processing or scrubbing
- policy hooks that may later influence provider/model choice

Do not build this on top of ambiguous backend state.

Deliverables:

- chat-level eval schema
- review pipeline for final answers
- tests covering visible-reasoning and plain-answer providers

Current first slice:

- `ai_chat_eval` records one lightweight evaluation row per final `reply` or
  `error`
- initial heuristics classify:
  - `reply_kind`
  - `quality_status`
  - `reasoning_status`
- a minimal user-feedback path now records `useful` or `not-useful` against
  the terminal reply evaluation row
- this is intentionally rule-based and deterministic until provider behavior
  and event semantics are more mature

Exit criteria:

- Fossil can evaluate agent replies without conflating backend progress,
  visible reasoning, and final response text

## Testing Strategy

Default `make test` should remain hermetic and Tcl-based.

Coverage to add or maintain:

- config precedence
- provider/model mismatch rejection
- user-config runtime fallback
- `/agentui` new-chat defaults
- explicit session reopen restores provider/model
- streaming success and partial-failure handling
- chat event persistence
- conversation evaluation records

Live-provider tests should remain opt-in and not be required for default
developer verification.

## Principles

- explicit provider identity beats command inference
- runtime diagnostics beat guesswork
- compatibility matters, but silent fallback should be minimized
- wrappers are implementation details, not product-level semantics
- streaming should be designed as transport, not bolted onto final text blobs
- evaluation should operate on structured outcomes, not raw ambiguous output
