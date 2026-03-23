# Fossil / Goose Integration Implementation Plan

## Goal

Implement a clean split where:

- Fossil C owns repository-native infrastructure, persistence, retrieval,
  backend execution, and safety-critical primitives
- TH1 owns orchestration policy, role behavior, prompt composition, and
  configurable workflow logic
- Goose owns higher-level agent management and the richer dynamic UI layer

This plan is ordered to preserve working behavior at every step and avoid a
large rewrite.

## Scope

In scope:

- refactoring Fossil's current AI/agent implementation
- defining a stable event/session/tool contract
- moving orchestration ownership into TH1
- removing HTML as the integration boundary
- preparing Goose to consume Fossil agent services

Out of scope for the first pass:

- replacing Fossil's repository-local storage model
- rewriting Fossil agent logic in Rust
- forcing Goose UI decisions into Fossil
- introducing mandatory network or cloud dependencies

## Success Criteria

The plan is complete when all of the following are true:

- `src/agent.c` no longer mixes storage, web transport, TH1 bridge, runtime,
  orchestration fallback, and tool registry in one file
- Fossil exposes a documented JSON/SSE event contract that does not depend on
  pre-rendered HTML
- default orchestration is loaded from TH1 assets rather than a large C string
- Fossil's built-in web UI consumes the same session/event APIs that Goose will
  use
- Goose can attach to Fossil-backed sessions using the shared contract

## Phase 0: Baseline And Freeze The Contract Surface

Objective:

- document exactly what exists today before refactoring

Steps:

1. inventory current endpoints and their payloads:
   - `/agent-config`
   - `/agent-history`
   - `/agent-events`
   - `/agent-chat`
   - `/agent-chat-stream`
   - `/agent-feedback`
   - `/agent-retrieval`
2. inventory current TH1 commands registered by `agent_register_th1`
3. inventory the current persisted event kinds in `agentchat`
4. write a single source-of-truth contract note for current behavior
5. add regression tests that freeze current working behavior before structural
   changes

Primary files:

- [agent.c](/home/bensiv/fossil-scm/src/agent.c)
- [AGENT_EVENTS.md](/home/bensiv/fossil-scm/doc/ai/AGENT_EVENTS.md)
- [TEST_PLAN.md](/home/bensiv/fossil-scm/doc/ai/TEST_PLAN.md)
- `tst/agent.test`
- `tst/th1-agent.test`

Exit criteria:

- there is a test-backed baseline for today's endpoints and event model

## Phase 1: Split `src/agent.c` By Responsibility

Objective:

- make the architectural seams explicit without changing user-visible behavior

Steps:

1. create `src/agent_store.c`
   - move session schema creation
   - move event persistence helpers
   - move session queries
   - move retrieval of history/events/config rows
2. create `src/agent_runtime.c`
   - move provider/model resolution
   - move backend execution
   - move streaming support
   - move validation helpers
3. create `src/agent_th1.c`
   - move TH1 command implementations
   - move `agent_register_th1`
4. create `src/agent_web.c`
   - move page handlers and API endpoints
   - keep this layer thin
5. create `src/agent_tools.c`
   - move tool primitives currently hidden under `agent_mcp_call`
6. reduce `src/agent.c` to shared declarations, command entry points, or
   compatibility shims as needed
7. update `src/main.mk` and related build files

Primary files:

- [agent.c](/home/bensiv/fossil-scm/src/agent.c)
- [main.mk](/home/bensiv/fossil-scm/src/main.mk)

Verification:

- build succeeds
- existing `tst/agent.test` behavior remains unchanged
- existing `/agentui` and API routes still work

Exit criteria:

- code layout reflects storage/runtime/web/TH1/tool boundaries

## Phase 2: Formalize The Internal C Interfaces

Objective:

- stop relying on file-local implicit coupling

Steps:

1. add an `agent.h` interface split by domain:
   - store APIs
   - runtime APIs
   - TH1 bridge APIs
   - web/API emitters
   - tool APIs
2. remove direct cross-file reach-ins where possible
3. define a shared event/session struct vocabulary in C
4. make all JSON emitters derive from shared data helpers instead of ad hoc SQL
5. add narrow helper functions for:
   - `agent_session_create`
   - `agent_session_get`
   - `agent_event_append`
   - `agent_event_list`
   - `agent_runtime_execute`
   - `agent_runtime_execute_stream`

Primary files:

- new or updated [agent.h](/home/bensiv/fossil-scm/src/agent.h)
- new `src/agent_*.c` files

Verification:

- no duplicated SQL or event formatting logic across files
- tests still pass

Exit criteria:

- Fossil has stable internal service interfaces for the agent stack

## Phase 3: Move Default Orchestration Ownership Into TH1

Objective:

- TH1 becomes the real policy layer

Steps:

1. move the built-in orchestration script out of the C string in
   [agent.c](/home/bensiv/fossil-scm/src/agent.c#L5575)
2. create a built-in TH1 asset path such as:
   - `cfg/roles/chat-default.th1`
   - or `cfg/roles/default.th1` if you want that to be canonical
3. implement lookup order:
   - explicit requested role script
   - checkout-local role script
   - built-in bundled TH1 asset fallback
4. make `/agent-chat` and `/agent-chat-stream` use the same TH1 loader path
5. remove any remaining baked-in orchestration policy from C except minimal
   fallback error handling
6. move response parsing policy into TH1 where practical
7. document the role/script contract

Primary files:

- [default.th1](/home/bensiv/fossil-scm/cfg/roles/default.th1)
- [reviewer.th1](/home/bensiv/fossil-scm/cfg/roles/reviewer.th1)
- new built-in TH1 asset wiring
- [TH1_ORCHESTRATION.md](/home/bensiv/fossil-scm/doc/ai/TH1_ORCHESTRATION.md)

Verification:

- all chat flows execute through loaded TH1 assets
- role switching still works
- no behavior regression in streaming mode

Exit criteria:

- C exposes primitives; TH1 controls orchestration

## Phase 4: Replace HTML-As-Data With A Structured Agent API

Objective:

- make JSON/SSE the integration boundary for both Fossil UI and Goose

Steps:

1. define versioned endpoints under a stable namespace, for example:
   - `/agent-api/v1/session/create`
   - `/agent-api/v1/session/get`
   - `/agent-api/v1/session/list`
   - `/agent-api/v1/event/list`
   - `/agent-api/v1/event/stream`
   - `/agent-api/v1/chat/submit`
   - `/agent-api/v1/feedback/submit`
   - `/agent-api/v1/context/get`
   - `/agent-api/v1/retrieval/get`
2. define canonical payload shapes for:
   - session
   - event
   - tool request
   - tool result
   - reply
   - error
3. make current endpoints either:
   - call the new service functions directly, or
   - become compatibility wrappers
4. stop pre-rendering `history_html` and `sessions_html` in C
5. keep raw data and rendering separate
6. write contract docs and JSON examples

Primary files:

- new `src/agent_api.c` or `src/agent_web.c`
- [AGENT_EVENTS.md](/home/bensiv/fossil-scm/doc/ai/AGENT_EVENTS.md)
- new API contract doc

Verification:

- Fossil UI can rebuild the current agent view from API responses alone
- event polling and SSE stream use the same underlying event contract

Exit criteria:

- HTML is no longer the shared module boundary

## Phase 5: Normalize The Event Model

Objective:

- make events rich enough for both Fossil UI and Goose without ad hoc parsing

Steps:

1. define canonical event kinds:
   - `prompt`
   - `progress`
   - `context`
   - `tool_request`
   - `tool_result`
   - `reply`
   - `error`
   - `feedback`
2. normalize `meta` into a documented JSON object schema
3. add explicit fields where they should not live inside `meta`
4. make streaming emit the same event kinds as persisted history
5. define terminal event semantics cleanly
6. define tool approval semantics as first-class events
7. update feedback attachment rules to target canonical reply/error events

Primary files:

- [agent_store layer](/home/bensiv/fossil-scm/src/agent.c#L1685)
- [AGENT_EVENTS.md](/home/bensiv/fossil-scm/doc/ai/AGENT_EVENTS.md)

Verification:

- UI clients do not rely on string matching in `meta`
- event replay can reconstruct session state deterministically

Exit criteria:

- event data is contract-grade, not implementation-grade

## Phase 6: Turn Tooling Into A Real Capability Layer

Objective:

- replace ad hoc tool behavior with explicit, auditable capability contracts

Steps:

1. extract current tool operations from `agent_mcp_call`
2. define capability metadata for each tool:
   - name
   - description
   - requires_write
   - requires_network
   - requires_confirmation
   - argument schema
3. expose capability listing through API and CLI
4. make TH1 orchestration consume capability declarations instead of implicit
   prompt assumptions
5. define approval binding to exact tool request payloads
6. persist tool-request and tool-result events
7. document the stable tool envelope format

Primary files:

- `src/agent_tools.c`
- `src/agent_runtime.c`
- [ORCHESTRATION_IMPLEMENTATION_PLAN.md](/home/bensiv/fossil-scm/doc/ai/ORCHESTRATION_IMPLEMENTATION_PLAN.md)

Verification:

- the UI can render available tools from structured data
- approval/rejection maps to a concrete persisted request id

Exit criteria:

- tools are explicit capabilities, not hidden branches in C code

## Phase 7: Convert Fossil's Web UI Into A Thin Client

Objective:

- make Fossil's own UI prove the contract works

Steps:

1. keep [agentui.th1](/home/bensiv/fossil-scm/cfg/agentui.th1) as a shell only
2. rewrite [agentui.js](/home/bensiv/fossil-scm/cfg/agentui.js) to:
   - fetch session lists via the agent API
   - fetch event history via the agent API
   - stream live events via the SSE API
   - render tool approvals from event data
3. stop embedding server-generated session/history markup
4. add clear client-side state for:
   - current session
   - stream position
   - pending tool approvals
   - selected terminal reply
5. preserve the simple Fossil-native UX while proving the new contract

Primary files:

- [agentui.th1](/home/bensiv/fossil-scm/cfg/agentui.th1)
- [agentui.js](/home/bensiv/fossil-scm/cfg/agentui.js)
- [agentui.css](/home/bensiv/fossil-scm/cfg/agentui.css)

Verification:

- Fossil UI renders entirely from structured API responses
- old pre-rendered HTML paths can be deleted

Exit criteria:

- Fossil UI is a client of the shared contract, not a special case

## Phase 8: Build The Goose Adapter Layer

Objective:

- let Goose talk to Fossil without absorbing Fossil internals

Steps:

1. create a Goose-side Fossil adapter module, likely under a dedicated crate or
   integration module
2. implement client operations for:
   - session list/create/get
   - event list/stream
   - chat submit
   - tool approval/rejection
   - context/retrieval inspection
3. map Fossil event kinds into Goose's internal event model
4. start with a read-only integration if needed:
   - list sessions
   - replay session history
   - observe live stream
5. then add interactive operations:
   - submit prompt
   - receive events
   - approve tool actions
6. keep adapter code isolated from Goose core generic agent logic

Primary files in Goose:

- new Fossil adapter module under `crates/goose`
- integration points near
  [agent.rs](/home/bensiv/goose/crates/goose/src/agents/agent.rs)
- session manager integration near
  [manager.rs](/home/bensiv/goose/crates/goose/src/execution/manager.rs)

Verification:

- Goose can observe and continue a Fossil-backed session
- event mapping is lossless enough for UI rendering

Exit criteria:

- Goose can operate as the higher-level interface for Fossil agent sessions

## Phase 9: Add Agent-Management Features In Goose

Objective:

- move multi-agent and richer orchestration UX to Goose where it fits best

Steps:

1. introduce a Fossil-backed session source in Goose's agent manager
2. support multiple Fossil sessions side by side
3. add session attach/detach semantics
4. render tool requests, approvals, and event timelines using Goose UI patterns
5. add richer diagnostics and filtering views on top of Fossil's event API
6. optionally add multi-agent coordination using Fossil session ids as backing
   identifiers

Primary files:

- Goose desktop UI components
- Goose streaming hooks and session management layers

Verification:

- Goose UI offers capabilities beyond Fossil's minimal built-in interface
- Fossil remains usable without Goose

Exit criteria:

- Goose is the advanced agent/UI module; Fossil remains the native core

## Phase 10: Testing And Verification

Objective:

- make the refactor safe and replayable

Steps:

1. expand Tcl tests for:
   - session lifecycle
   - event persistence
   - role loading
   - orchestration through TH1
   - tool request / approval flow
2. add JSON contract tests:
   - endpoint payload structure
   - event stream ordering
   - compatibility wrappers
3. add Fossil UI integration tests where practical
4. add Goose adapter integration tests against a Fossil fixture server
5. add migration tests for existing repositories with current `agentchat` data
6. add performance checks around:
   - context assembly
   - event streaming
   - history replay

Primary files:

- `tst/agent.test`
- `tst/th1-agent.test`
- Goose integration tests

Exit criteria:

- the new contract and refactor are test-backed across both repositories

## Phase 11: Migration And Cleanup

Objective:

- finish the transition without leaving duplicate paths indefinitely

Steps:

1. deprecate old endpoint names once compatibility wrappers exist
2. remove legacy HTML-precompute helpers
3. remove the embedded C orchestration string
4. remove unused compatibility shims from `src/agent.c`
5. update docs to point to the new ownership model
6. add a small migration note for contributors describing:
   - where to put C logic
   - where to put TH1 logic
   - where Goose-side code belongs

Exit criteria:

- the codebase reflects the intended architecture in both file layout and docs

## Recommended Delivery Order

If you want the lowest-risk sequence, do the work in this order:

1. Phase 0
2. Phase 1
3. Phase 2
4. Phase 3
5. Phase 4
6. Phase 5
7. Phase 7
8. Phase 6
9. Phase 8
10. Phase 9
11. Phase 10
12. Phase 11

Rationale:

- first create structural seams
- then move orchestration ownership
- then define and consume the shared API
- then integrate Goose

## First Concrete Work Batch

The first implementation batch should be intentionally narrow:

1. split `src/agent.c` into `agent_store.c`, `agent_runtime.c`, `agent_th1.c`,
   and `agent_web.c`
2. move the built-in orchestration string into a TH1 asset file
3. add a minimal versioned event/session API
4. make Fossil's own UI consume that API for sessions and event history

That batch gives you a real architectural seam quickly, without yet requiring
the Goose-side adapter.
