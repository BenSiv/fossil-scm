# Fossil Internal Agent Next Steps

## Scope

This is the immediate execution plan from the current checkpoint.

It assumes:

- Fossil is the only product surface that matters
- `agent-api-v1-*` is an internal UI/tooling boundary
- the current `agent` Tcl suite is green
- the next work should deepen Fossil's request/tool lifecycle, not widen the API

## Current Baseline

Verified at this checkpoint:

- `make -C /home/bensiv/fossil-scm`
- `tclsh tst/tester.tcl ./bin/fossil agent`
- the broad full suite advances past `agent` and `ai` without the old `fossil agent` hang

Recently stabilized:

- `fossil agent ...` dispatch now reaches the real CLI path instead of the MCP stdio loop
- builtin asset fallback works when checkout-local files are absent
- `agentui` and `agent.test` are aligned with the request-aware event model
- approval apply is routed through Fossil instead of the old confirmed-edit chat hack

## Phase 1: Complete Approval Lifecycle

Goal:

- make approval a real server-owned request state, not just a UI convention

Steps:

1. persist explicit approval decisions:
   - `waiting-approval`
   - `approved`
   - `rejected`
2. add a dedicated rejection route alongside `agent-api-v1-approval-apply`
3. emit explicit approval events:
   - `approval_requested`
   - `approval_applied`
   - `approval_rejected`
4. make request terminal state derive from the approval outcome instead of UI timing

Tests:

- extend `tst/agent-v1-smoke.test`
- add approval rejection coverage
- add state transition assertions on `agent_request`

## Phase 2: Turn The Tool Registry Into A Real Tool Runtime

Goal:

- move from descriptive tool metadata to enforceable server-side tool execution rules

Steps:

1. classify tools by execution and safety class:
   - read-only
   - write
   - network
   - confirm-required
2. persist tool invocation records with:
   - tool name
   - phase
   - request id
   - approval requirement
   - result status
3. add built-in execution handlers for the default internal tools:
   - read file
   - search files
   - repo inspection
   - patch/apply
4. keep TH1 responsible for deciding which tool to ask for, but keep C responsible for whether it can run

Tests:

- add tool registry endpoint assertions
- add tool request/result event assertions
- add deny/confirm-path tests for write tools

## Phase 3: Tighten Agent UI Around Request And Tool Cards

Goal:

- make `agentui` reflect the request lifecycle directly instead of treating chat rows as the primary model

Steps:

1. promote the request panel into the main state display
2. render tool request/result rows as structured cards
3. render approval-needed rows as dedicated approval cards
4. keep polling for now; do not add SSE until the event model is fully stable
5. keep the JS small and state-light

UI rule:

- server owns truth
- JS only mirrors current request/event state

Tests:

- extend `tst/agent.test` only where the UI contract is now stable
- prefer API and smoke assertions over brittle HTML string matching

## Phase 4: Normalize Event Semantics

Goal:

- settle the internal event contract before adding more features

Target event kinds:

- `message`
- `progress`
- `tool_request`
- `tool_result`
- `approval_requested`
- `approval_applied`
- `approval_rejected`
- `error`
- `finish`

Steps:

1. stop overloading legacy `role` and `kind` combinations
2. make `event_type` the canonical UI field
3. ensure every event that belongs to a request includes `request_id`
4. ensure every terminal path marks `is_terminal` coherently

## Phase 5: Resume Broad-Suite Hardening

Goal:

- use the now-green `agent` suite as the base for broader regression confidence

Steps:

1. rerun the full Tcl suite to completion
2. confirm there is no remaining fallout from the `agent` command-dispatch fix
3. only then resume changes to more ambitious tooling/runtime behavior

## Recommended Order

1. approval rejection route and state persistence
2. tool runtime enforcement and execution metadata
3. `agentui` request/tool card cleanup
4. event-type normalization
5. full-suite completion check

## Explicit Non-Goals For The Next Pass

Do not spend the next pass on:

- Goose integration
- widening the API for third-party clients
- SSE or websocket work
- a larger frontend rewrite
- making Fossil look like a desktop-app replacement

## Exit Condition

This next phase is done when:

- approval lifecycle is server-owned and test-covered
- tool request/result flow is explicit and enforceable
- `agentui` shows request/tool/approval state coherently
- the internal event contract is narrow enough to stay stable
