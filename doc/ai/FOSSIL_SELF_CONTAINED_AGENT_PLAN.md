# Fossil Self-Contained Agent Plan

## Goal

Turn Fossil into a self-contained local-first agent and knowledge system with:

- Fossil as the only required runtime and primary UI
- C owning infrastructure, persistence, eventing, permissions, tool execution,
  and performance-sensitive paths
- TH1 owning orchestration policy, roles, prompt composition, and configurable
  workflow behavior
- the current Goose integration reduced to a temporary compatibility and
  reference path rather than a permanent product dependency

This plan assumes the long-term target is to make Goose optional and eventually
unnecessary for day-to-day use.

## Product Position

The desired product shape is:

- one Fossil binary
- one repository-native storage model
- one web UI
- one local-first deployment and permission model
- one maintainable implementation stack with limited JS used only for
  progressive enhancement

That means the work is not "port Goose into Fossil." It is:

1. identify the capabilities Goose currently demonstrates better
2. rebuild the necessary subset in Fossil-native form
3. preserve Fossil's architectural strengths while doing so

## Success Criteria

This plan is complete when all of the following are true:

- Fossil can run agent sessions, tool calls, approvals, and knowledge workflows
  without any Goose dependency
- Fossil exposes a stable internal and external event model for requests,
  messages, tools, and approvals
- Fossil web UI supports the core interactive workflow:
  - session list
  - session resume
  - live request progress
  - tool request/result display
  - cancellation
  - approval prompts
  - knowledge browsing
- TH1 controls roles, workflows, and policy decisions instead of C embedding
  the control logic
- the default tooling and knowledge pipeline are bundled and work in a
  repository-local setup
- Goose can be treated as optional compatibility tooling or retired entirely

## Main Gaps To Close

The main things Goose currently demonstrates better than Fossil are:

1. structured request lifecycle
2. rich tool-call and approval UX
3. resumable streaming/session event behavior
4. explicit backend capability modeling
5. integrated multi-step agent shell behavior

The plan below targets those gaps directly.

## Phase 1: Stabilize Fossil As The Only Agent Backend

Objective:

- make Fossil's own request/session/event model complete enough that it can be
  the authoritative runtime, not just a partial backend

Deliverables:

1. finish route stabilization around flat Fossil-native endpoint names
2. normalize all agent responses around stable envelopes:
   - `api_version`
   - `ok`
   - `error`
   - `error_code`
   - `capabilities`
   - `request_id`
3. introduce explicit request lifecycle records in storage:
   - queued
   - running
   - waiting-approval
   - cancelled
   - failed
   - finished
4. add cancellation and active-request tracking as first-class Fossil concepts
5. make the current browser UI consume those states consistently

Primary modules:

- [agent_store.c](/home/bensiv/fossil-scm/src/agent_store.c)
- [agent_runtime.c](/home/bensiv/fossil-scm/src/agent_runtime.c)
- [agent_web.c](/home/bensiv/fossil-scm/src/agent_web.c)
- [agent_internal.h](/home/bensiv/fossil-scm/src/agent_internal.h)

Exit criteria:

- Fossil is a coherent agent backend even with Goose absent

## Phase 2: Build A First-Class Tool And MCP Layer In Fossil

Objective:

- make tools a real platform feature instead of ad hoc backend behavior

Deliverables:

1. define a Fossil-native tool registry abstraction
2. model tools with explicit metadata:
   - name
   - description
   - input schema
   - output shape
   - permission characteristics
   - confirm/approval policy
3. implement bundled default tools for:
   - shell execution
   - file read/search
   - file edit/apply patch
   - repository status/log/diff
   - knowledge retrieval
   - note/artifact creation
4. define transport adapters for external tools or MCP-like processes
5. persist tool requests/results as structured events, not plain text only

Primary modules:

- new `src/agent_tools.c`
- new `src/agent_mcp.c`
- [agent_runtime.c](/home/bensiv/fossil-scm/src/agent_runtime.c)
- [agent_th1.c](/home/bensiv/fossil-scm/src/agent_th1.c)

Exit criteria:

- Fossil has a stable built-in tool platform with bundled defaults

## Phase 3: Make TH1 The True Policy Layer

Objective:

- keep C focused on capability and state, while TH1 controls behavior

Deliverables:

1. move default role/workflow behavior entirely into TH1 assets
2. expose structured tool and request primitives to TH1
3. let TH1 define:
   - phases
   - roles
   - prompt assembly
   - tool selection policy
   - approval strategy
   - knowledge promotion heuristics
4. reduce remaining hardcoded orchestration decisions in C
5. support repository-local overrides safely

Primary modules:

- [agent_th1.c](/home/bensiv/fossil-scm/src/agent_th1.c)
- [cfg/roles/](/home/bensiv/fossil-scm/cfg/roles/)
- new workflow/policy docs under `doc/ai/`

Exit criteria:

- C is capability and storage
- TH1 is behavior and workflow

## Phase 4: Upgrade Fossil UI Into A Real Agent Console

Objective:

- provide the minimum interactive shell Fossil needs without turning the UI
  into a SPA

UI doctrine:

- server-rendered HTML remains the default
- forms and links remain valid without JS where practical
- small bundled JS is allowed only for progressive enhancement:
  - partial refresh
  - SSE or polling updates
  - inline approval/cancel controls
  - incremental message rendering

Deliverables:

1. replace the current simple chat console with an event-driven session view
2. add live request status and streaming/progress updates
3. add structured tool-call cards:
   - tool request
   - tool result
   - waiting approval
4. add request cancellation and retry controls
5. show session capability state explicitly in the UI
6. integrate knowledge context and retrieval summaries into the session view

Primary assets:

- [cfg/agentui.th1](/home/bensiv/fossil-scm/cfg/agentui.th1)
- [cfg/agentui.js](/home/bensiv/fossil-scm/cfg/agentui.js)
- [agent_web.c](/home/bensiv/fossil-scm/src/agent_web.c)

Exit criteria:

- Fossil UI covers the essential interactive workflow without requiring Goose

## Phase 5: Turn The AI Pool Into A Full Knowledge System

Objective:

- make knowledge a core Fossil-native feature, not a side table

Deliverables:

1. strengthen note and artifact linking
2. improve retrieval diagnostics and ranking transparency
3. add explicit promotion/curation workflows
4. improve browser views for:
   - notes
   - reviews
   - retrieval traces
   - materialized artifacts
5. connect knowledge actions into agent workflows and tool calls

Primary modules:

- [ai.c](/home/bensiv/fossil-scm/src/ai.c)
- knowledge browser docs and UI handlers
- retrieval and review reporting code

Exit criteria:

- Fossil is not just an agent runner but a durable knowledge system

## Phase 6: Bundle A Default Fossil Agent Distribution

Objective:

- make the self-contained path work out of the box

Deliverables:

1. builtin role assets and default policy assets
2. builtin default tooling manifests
3. sane default config examples
4. diagnostics and verification commands for local setup
5. explicit separation between optional provider helpers and required core

Exit criteria:

- a new Fossil checkout can enable the agent system without requiring Goose

## Phase 7: Deprecate Goose As A Product Dependency

Objective:

- reduce Goose from product dependency to optional compatibility path

Deliverables:

1. stop treating Goose as the primary rich client for planned features
2. keep only one of:
   - a compatibility client for testing
   - protocol fixtures/docs
   - nothing, if no longer useful
3. update docs to describe Fossil as the canonical agent system
4. freeze or retire Fossil-specific Goose adapter code as appropriate

Exit criteria:

- Fossil stands alone as the maintained agent + knowledge product

## Implementation Order

Recommended order of execution:

1. stabilize request/session/event lifecycle in Fossil
2. implement a real built-in tool/MCP layer
3. move policy fully into TH1
4. build the interactive Fossil UI shell
5. deepen the knowledge system
6. bundle defaults and diagnostics
7. deprecate Goose integration

This order matters because UI work becomes much cleaner once the event and tool
models are stable, and Goose can only be retired safely once Fossil's own UI
and tooling surface is complete.

## Explicit Non-Goals

To preserve maintainability, this plan does not aim to:

- recreate Electron-specific desktop affordances inside Fossil
- import Goose runtime source directly into Fossil
- replace the C/TH1 split with a larger language surface
- build a heavy SPA inside Fossil

## Recommended Immediate Next Steps

The next concrete tasks after adopting this plan are:

1. finish Fossil-native flat API route migration end to end
2. define and persist a first-class request lifecycle table
3. normalize stored event kinds for messages, progress, tools, approvals, and
   finish states
4. prototype approval and tool-result cards in the Fossil web UI
5. define the first bundled Fossil tool registry schema

## Relationship To Existing Docs

This document supersedes the long-term product direction implied by the earlier
Goose integration notes, but it does not discard that work. Existing Goose
integration documents still matter as:

- contract notes
- migration history
- comparative design references

The operative direction, however, is Fossil-first and Fossil-complete.
