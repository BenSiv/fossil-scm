# Fossil Internal Agent Tooling Plan

## Purpose

This is the active, condensed plan for the agent work in this fork.

The scope is now narrower than the earlier Fossil/Goose integration plans:

- Fossil is the product
- Fossil's web UI is the primary UI
- agent routes are an internal UI/tooling boundary first
- Goose is reference material, not the target runtime or primary client

The design target is one local-first Fossil system with:

- C owning persistence, execution, permissions, eventing, and performance paths
- TH1 owning roles, prompt/workflow policy, and repository-local behavior
- a thin Fossil web UI using only small progressive-enhancement JS where needed

## Current State

Already in place:

- `src/agent.c` has been split into narrower service slices
- Fossil-native flat agent routes are live under `agent-api-v1-*`
- request records are first-class storage objects
- event payloads now expose normalized fields such as:
  - `event_type`
  - `request_id`
  - `is_terminal`
- `cfg/agentui.js` now consumes request-aware chat/event responses
- builtin-backed asset loading works for role and prompt assets
- `tst/agent-v1-smoke.test` exists as the focused regression harness for this surface

Not yet complete:

- approval and cancellation lifecycle
- structured tool-call platform and MCP-like adapters
- richer event-driven agent UI
- a fully TH1-driven workflow/policy layer
- stronger knowledge-tool integration in the session workflow

## Active Priorities

### 1. Finish The Request Lifecycle

Keep the current flat route family, but treat it as Fossil-internal tooling
surface, not a public platform commitment.

Near-term work:

1. keep request objects explicit in all chat/session/event responses
2. add lifecycle states needed by the Fossil UI:
   - `running`
   - `waiting-approval`
   - `cancelled`
   - `failed`
   - `finished`
3. add a focused cancel/active-request model only if the Fossil runtime can
   honor it coherently
4. keep tests centered on the flat route family and payload stability

### 2. Build The Tool Layer Inside Fossil

The main missing capability is not chat. It is tool orchestration.

Near-term work:

1. define a Fossil-native tool registry
2. model tool request and tool result events explicitly
3. ship default built-in tools for:
   - shell execution
   - file read/search
   - patch/apply
   - repository inspection
   - knowledge retrieval
4. add approval hooks before dangerous tools execute

### 3. Move More Behavior Into TH1

The rule remains:

- C handles capability, storage, execution, and safety
- TH1 handles policy and workflow behavior

Near-term work:

1. keep default orchestration and role behavior in bundled TH1 assets
2. expose structured request/tool primitives to TH1
3. reduce hardcoded orchestration branches in C
4. support repository-local overrides without making core behavior opaque

### 4. Upgrade Fossil UI, But Keep It Thin

The UI target is an event-driven server-rendered console, not a SPA.

Near-term work:

1. keep HTML server-rendered by default
2. use small JS only for:
   - polling or SSE
   - partial updates
   - request status
   - tool approval controls
3. render request lifecycle directly in `agentui`
4. add tool cards and approval prompts once the tool layer exists

### 5. Tighten The Regression Gate

The broad historical `tst/agent.test` suite is still noisy.

Active rule:

- use `tst/agent-v1-smoke.test` as the clean regression gate for this work
- expand it incrementally with session rename, capabilities, unsupported ops,
  and request lifecycle assertions
- only broaden legacy suite coverage after the focused harness stays stable

## What This Plan De-Prioritizes

These are no longer the main driver of the work:

- making Fossil look like a general external backend product
- shaping the route contract primarily for Goose desktop
- maintaining Goose as a coequal rich client
- widening the API surface before the Fossil UI and runtime need it

## Exit Condition

This phase is successful when Fossil can stand on its own as:

- a self-contained local-first agent runtime
- a maintainable thin web UI with progressive enhancement
- a request-aware and tool-aware knowledge workflow system

At that point, any remaining Goose compatibility becomes optional, not
architecturally central.
