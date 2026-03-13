# AI Integration Implementation Plan

This plan turns the current agent integration into a clearer provider-aware
system without abandoning the existing local-wrapper approach.

## Goals

- make runtime config resolution inspectable
- remove backend/model ambiguity
- preserve simple local integrations
- prepare for streaming and richer event handling

## Phase 1: Visibility

### 1. Effective config diagnostics

Add UI and server visibility for:

- effective config source
- effective chat command/provider
- effective chat model
- effective embedding command/provider
- effective embedding model

Deliverables:

- `/agentui` debug panel or status line
- helper functions that report resolved config values and where they came from

### 2. Runtime precedence audit

Make precedence explicit and testable:

- CLI override
- environment override
- repo setting
- user config
- checkout config
- repo fallback

Deliverables:

- doc updates
- regression tests for precedence

## Phase 2: Provider Identity

### 3. Introduce explicit provider fields

Add support for:

- `provider`
- `embedding_provider`

Keep current fields working for compatibility:

- `command`
- `embedding_command`

Deliverables:

- schema-less JSON compatibility in `ai-agent.json`
- runtime resolution that infers provider from legacy config when missing

### 4. Provider-aware validation

Before invoking a backend, validate:

- provider/model pairing
- provider/embedding_model pairing
- minimum required command or binary presence

Deliverables:

- early user-facing errors
- no more accidental `auto` sent to Ollama
- no more Ollama model names sent to Codex

## Phase 3: UI Behavior

### 5. Provider-aware UI controls

Replace or supplement the free-form model field with:

- provider selection
- model selection or provider-scoped model text field

At minimum, display the active provider beside the model field.

Deliverables:

- no stale backend ambiguity in `/agentui`
- explicit link between displayed model and runtime backend

### 6. Session metadata improvements

Store per-session backend identity in addition to model text.

Deliverables:

- session reopen shows correct backend + model
- past sessions remain attributable even if config changes later

## Phase 4: Streaming

### 7. Streaming chat transport

Move `/agent-chat` from buffered JSON replies to streamed output.

Candidate approaches:

- chunked HTTP
- server-sent events

Deliverables:

- progressive agent output in `/agentui`
- preserved final persisted reply after stream completion

### 8. Structured message parts

Stop treating all output as one text blob.

Introduce message part types such as:

- `progress`
- `reasoning_visible`
- `tool_activity`
- `final_text`
- `error`

Deliverables:

- cleaner UI rendering
- future filtering or evaluation hooks

## Phase 5: Evaluation Loop Expansion

### 9. Keep the current retrieval loop

The existing retrieval evaluation loop should remain:

- retrieval reinforcement
- co-retrieval links
- duplicate detection
- title correction
- metadata normalization

### 10. Add conversation-level evaluation later

Only after provider/model/config clarity is in place should Fossil add:

- reply quality review
- output classification
- visible reasoning filtering or handling
- provider-specific post-processing

This avoids building evaluation logic on top of ambiguous backend state.

## Testing Plan

Add tests for:

- config precedence
- provider/model mismatch rejection
- user-config runtime fallback
- first-load `/agentui` starts new chat
- explicit session reopen restores correct backend/model
- streaming success and partial failure handling

## Principles

- explicit provider identity beats command inference
- runtime diagnostics beat guesswork
- compatibility matters, but silent fallback should be minimized
- wrappers are implementation details, not product-level semantics
