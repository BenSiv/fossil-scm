# Fossil / Goose Agent Architecture Audit

## Purpose

This note maps the current Fossil AI architecture as implemented today, checks it
against the intended C vs TH1 boundary, and proposes a clean extraction path for
an agent-management and dynamic-UI module that can align with Goose.

## Executive Summary

The current Fossil implementation is only partially at the intended split.

- The high-value infrastructure and performance-sensitive paths are already in C.
- The orchestration layer exists in TH1, but only as a thin layer on top of a
  large `src/agent.c`.
- The dynamic UI is visually templated in TH1 and JS, but its lifecycle,
  endpoints, persistence, and tool plumbing still live in C.
- Goose already has the clearer separation you want:
  interface -> agent manager -> agent -> extensions/tools.

The right extraction is not "move agent features out of C." It is:

1. Keep Fossil core state, storage, retrieval, process control, and low-level
   tool execution in C.
2. Move orchestration policy fully into TH1.
3. Split `src/agent.c` into a reusable C service layer plus a thin Fossil web
   adapter.
4. Define a neutral event/session/tool contract that Goose can consume.
5. Let Goose provide the richer dynamic UI and agent-management shell on top of
   that contract.

## What Is In C Today

### Correctly in C

These belong in C and should stay there:

- TH1 runtime integration and interpreter hosting in [th_main.c](/home/bensiv/fossil-scm/src/th_main.c#L18)
- AI schema, note storage, retrieval scoring, vector distance, and audit data in
  [ai.c](/home/bensiv/fossil-scm/src/ai.c)
- Agent/session/event persistence in
  [agent.c](/home/bensiv/fossil-scm/src/agent.c#L1682)
- Context assembly exposed to TH1 through `agent_context` in
  [agent.c](/home/bensiv/fossil-scm/src/agent.c#L5866)
- Backend execution exposed through `agent_run` and `agent_run_stream` in
  [agent.c](/home/bensiv/fossil-scm/src/agent.c#L5902)
- Pool operations and note graph helpers exposed to TH1 in
  [agent.c](/home/bensiv/fossil-scm/src/agent.c#L6176)

This is the right side of the boundary. It is stateful, performance-sensitive,
and tightly coupled to Fossil repository data and SQLite.

### Still Too Much In C

These parts are still over-concentrated in `src/agent.c`:

- Chat session schema and event model
  [agent.c](/home/bensiv/fossil-scm/src/agent.c#L1685)
- Session selection and title management
  [agent.c](/home/bensiv/fossil-scm/src/agent.c#L1750)
- Session/event rendering for the UI
  [agent.c](/home/bensiv/fossil-scm/src/agent.c#L1957)
- Web endpoints for config, history, events, pool, retrieval, feedback
  [agent.c](/home/bensiv/fossil-scm/src/agent.c#L5412)
- Built-in orchestration script embedded as a C string
  [agent.c](/home/bensiv/fossil-scm/src/agent.c#L5575)
- UI bootstrap and template variable wiring
  [agent.c](/home/bensiv/fossil-scm/src/agent.c#L5302)
- Ad hoc tool layer under `agent_mcp_call`
  [agent.c](/home/bensiv/fossil-scm/src/agent.c#L5953)

This is the main reason the architecture still feels blurry.

## What Is In TH1 Today

### Correctly in TH1

The intended orchestration direction is already visible:

- TH1 role scripts in
  [default.th1](/home/bensiv/fossil-scm/cfg/roles/default.th1) and
  [reviewer.th1](/home/bensiv/fossil-scm/cfg/roles/reviewer.th1)
- TH1 UI template in
  [agentui.th1](/home/bensiv/fossil-scm/cfg/agentui.th1)
- Documented "C muscle / TH1 brain" design in
  [TH1_ORCHESTRATION.md](/home/bensiv/fossil-scm/doc/ai/TH1_ORCHESTRATION.md)

This part matches your goal: policy, prompt construction, role behavior,
response parsing, and configurable orchestration should be scriptable.

### What Is Missing

TH1 is not yet the true control plane because:

- the default chat flow still depends on a built-in TH1 string in C instead of
  a repository-owned script
- the event model is designed in C and only consumed from TH1
- tool semantics are still hardcoded in C
- UI state shape is decided by C-generated HTML and JSON endpoints

So TH1 currently decorates the system more than it governs it.

## What The UI Looks Like Today

The UI is currently a mixed layer:

- HTML structure in [agentui.th1](/home/bensiv/fossil-scm/cfg/agentui.th1)
- browser behavior in [agentui.js](/home/bensiv/fossil-scm/cfg/agentui.js)
- Fossil page assembly in [agent.c](/home/bensiv/fossil-scm/src/agent.c#L5302)
- event/history/config APIs in [agent.c](/home/bensiv/fossil-scm/src/agent.c#L5412)

That means the "dynamic UI layer" is not actually separate today. Only its skin
is separate. The runtime contract still belongs to `src/agent.c`.

## What Goose Already Has That Fossil Lacks

Goose is closer to the target decomposition:

- clear agent object boundary in
  [agent.rs](/home/bensiv/goose/crates/goose/src/agents/agent.rs#L137)
- clear agent/session manager boundary in
  [manager.rs](/home/bensiv/goose/crates/goose/src/execution/manager.rs#L19)
- session model isolated under
  [session/mod.rs](/home/bensiv/goose/crates/goose/src/session/mod.rs)
- UI separated from agent core in
  [ChatContext.tsx](/home/bensiv/goose/ui/desktop/src/contexts/ChatContext.tsx) and
  [useChatStream.ts](/home/bensiv/goose/ui/desktop/src/hooks/useChatStream.ts)
- extension/tool protocol treated as first-class inside the agent core

Goose’s architectural shape is:

- interface layer
- agent manager
- per-session agent
- extension/tool execution
- session persistence

Fossil’s current shape is more:

- Fossil web endpoints
- one large mixed `agent.c`
- TH1 scripts riding on top
- repository/SQLite state

That is why Goose is the right place to host richer agent management and a more
dynamic UI shell.

## Recommended Boundary

### Keep In Fossil C

These should remain native:

- repository-aware context assembly
- retrieval, scoring, and vector math
- AI note pool and graph storage
- session/event persistence in repository SQLite
- backend process spawning and streaming
- permission and repository safety checks
- TH1 host/runtime and primitive commands

### Keep In Fossil TH1

These should become primarily TH1-owned:

- role definitions
- prompt construction
- reply parsing
- provider/model fallback policy
- task recipes
- lightweight tool choreography
- human-visible workflow logic

### Extract Into A Shared Agent/UI Module

This module should own:

- agent session lifecycle
- event protocol
- tool request / approval protocol
- dynamic UI rendering and interaction
- multi-agent management
- richer live streaming client behavior

This is the part that should be combined with Goose.

## The Correct Module Shape

Do not extract by copying Fossil UI code into Goose. Extract by defining a
stable contract.

### Proposed Contract

Fossil should expose a minimal agent service contract:

- `session.create`
- `session.get`
- `session.list`
- `event.list`
- `event.stream`
- `chat.submit`
- `feedback.submit`
- `context.inspect`
- `retrieval.inspect`
- `tool.invoke`
- `tool.approve`
- `tool.reject`

The payload model should be event-first, not HTML-first.

Core event types:

- `prompt`
- `progress`
- `context`
- `tool_request`
- `tool_result`
- `reply`
- `error`
- `feedback`

Once Fossil exposes that contract cleanly, Goose can be the higher-level UI and
agent manager without needing to know Fossil internals.

## Refactor Plan

### Phase 1: Split `src/agent.c` Internally

Create explicit internal slices:

- `agent_store.c`
  session/event schema, persistence, queries
- `agent_runtime.c`
  backend execution, streaming, provider resolution
- `agent_th1.c`
  TH1 command registration and bridging
- `agent_web.c`
  Fossil page and JSON/SSE endpoints
- `agent_tools.c`
  tool primitives exposed to TH1

This is still all C, but it will make the real boundaries visible.

### Phase 2: Remove Built-In Orchestration From C

Replace the embedded script in
[agent.c](/home/bensiv/fossil-scm/src/agent.c#L5575) with lookup order:

1. repo/local TH1 role script
2. built-in fallback TH1 asset

That makes TH1 the real policy layer.

### Phase 3: Define A Stable Event API

Normalize the current `/agent-config`, `/agent-history`, `/agent-events`,
`/agent-chat`, `/agent-chat-stream`, `/agent-feedback`, `/agent-retrieval`
endpoints into a versioned API contract.

Do not let UI templates depend on internal SQL or ad hoc HTML fragments.

### Phase 4: Remove UI Rendering From Core Agent Logic

Stop generating session/history HTML in C:

- replace `history_html` and `sessions_html` pre-rendering with structured JSON
- keep Fossil’s local web UI as a thin client over the same event/session APIs
- let Goose consume the same APIs

### Phase 5: Move Agent Management To Goose

Goose should become the home for:

- multi-agent lifecycle
- agent registry / manager
- richer approvals and tool UX
- parallel session views
- dynamic stream rendering
- more advanced frontend state transitions

Fossil remains the repository-native engine and source of truth.

## Design Rules Going Forward

Use these rules to keep the split clean:

- If it needs repository state, SQLite joins, vector search, or careful process
  control, write it in C.
- If it changes often with prompts, roles, workflows, or response parsing, put
  it in TH1.
- If it is primarily about session orchestration UX, multi-agent state, or rich
  live interaction, put it in the Goose-side module.
- Do not embed policy as giant C string constants.
- Do not let HTML fragments be the integration boundary.
- Make events the integration boundary.

## Current Reality Check

Your stated target is:

- core infrastructure logic and high-performance needs in C
- scriptable/configurable code in TH1
- agent management and dynamic UI extracted and combined with Goose

That target is sound.

The current codebase is close enough to support it, but not yet cleanly split.
The main blocker is not the AI storage or retrieval design. The blocker is that
`src/agent.c` still mixes:

- storage
- runtime
- web transport
- UI assembly
- tool protocol
- fallback orchestration

That file needs to become a set of narrower service layers before the Goose
integration will feel natural.

## Suggested Next Implementation Step

If you want the first concrete move, do this:

1. split session/event persistence out of `src/agent.c`
2. split TH1 bridge registration out of `src/agent.c`
3. replace pre-rendered HTML session/history blobs with JSON endpoints only
4. make the Fossil UI consume those JSON endpoints
5. then point Goose at the same contract

That sequence gives you a real module seam without destabilizing the C core.
