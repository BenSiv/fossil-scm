# AI Integration Implementation Plan

This plan moves Fossil from a wrapper-driven agent integration to a
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

## Phase D: Streaming And Structured Events

Objective:

- move from buffered one-shot replies to streamed, typed output

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
