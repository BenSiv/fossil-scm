# Tcl Test Suite Revival Plan

## Goal

Restore [`tst/tester.tcl`](../../tst/tester.tcl) as the authoritative
regression harness for `make test`, keeping Fossil's testing surface narrow,
deterministic, and aligned with the original Tcl-based workflow.

## Why Tcl

- It matches the existing Fossil test architecture.
- It avoids adding a second long-term test framework.
- It keeps developer prerequisites small: Fossil build deps plus Tcl.
- It reduces feature-specific knowledge required to add or debug tests.

## Current Problems

1. [`tst/tester.tcl`](../../tst/tester.tcl) exits immediately after printing
   `Ok`, so `make test` is a false green.
2. The harness behavior is under-specified for modern build expectations:
   pass/fail signaling, diagnostics, temporary isolation, and web request
   correctness need to be made explicit.
3. AI and agent regressions now span CLI, web, config resolution, and
   repository-side state changes, but current Tcl coverage only exercises a
   subset of the data-pool logic.

## Principles

- `make test` must run only hermetic tests.
- Default tests must not require network, Ollama, Codex, or user-local config.
- Helper behavior must be deterministic and explicit.
- New AI/agent coverage should land inside the Tcl harness, not in a permanent
  parallel framework.

## Phases

### Phase 1: Restore the runner

- Remove the unconditional early exit from `tester.tcl`.
- Ensure non-zero exit status when any test fails.
- Keep the current helper API intact unless a change is required for
  correctness.
- Verify that a targeted Tcl run such as `make test TESTFLAGS=ai` exercises
  real tests again.

### Phase 2: Tighten harness correctness

- Audit core helpers in `tester.tcl`:
  - `test_setup`
  - `test_cleanup`
  - `fossil`
  - `test_start_server`
  - `test_stop_server`
  - `test_fossil_http`
- Add a minimal harness self-check file to validate:
  - temp repo creation
  - cleanup
  - HTTP request handling
  - failure reporting

### Phase 3: Port AI/agent regressions into Tcl

- Extend [`tst/ai.test`](../../tst/ai.test) or split coverage into:
  - `tst/ai-core.test`
  - `tst/agent.test`
- Use a hermetic fake backend fixture for:
  - chat replies
  - embedding generation
- Cover:
  - `--agent-config`
  - `FOSSIL_AGENT_CONFIG`
  - repo `agent-config-path`
  - user config fallback
  - `agent note`
  - `agent embed`
  - `agent semantic-index`
  - `agent retrieve`
  - first-use `/agentui`
  - first-use `/agent-chat`
  - session persistence
  - effective model/config display

### Phase 4: Stabilize `make test`

- Confirm the default suite is hermetic and reproducible on a clean machine.
- Separate any future live-provider coverage into an explicit opt-in target.
- Document the expected `make test` contract.

## Acceptance Criteria

- `make test` runs real Tcl tests rather than a stub.
- Any failing test causes a non-zero exit code.
- Default tests remain hermetic.
- AI/agent coverage is implemented inside the Tcl harness.
- Developers only need the standard Fossil build prerequisites plus Tcl.

## Phase 1 Status

Phase 1 begins by reactivating `tester.tcl` and verifying that targeted Tcl
tests run through the existing `make test` entry point.
