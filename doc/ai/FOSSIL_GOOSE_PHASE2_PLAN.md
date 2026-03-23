# Fossil / Goose Phase 2 Development Plan

## Purpose

This plan replaces the earlier integration roadmap as the primary forward plan
after the initial module split, v1 API surfacing, and first Goose compatibility
adapter.

The goal from this point is to finish the Fossil <-> Goose boundary as a
deliberate contract, tighten tests around it, reconcile licensing strategy for
shared work, and update both repositories to reflect the new architecture.

## Current State

What is already true:

- Fossil has been split into explicit agent slices:
  - `agent_store.c`
  - `agent_runtime.c`
  - `agent_th1.c`
  - `agent_web.c`
- Fossil exposes an initial versioned agent API surface and builtin-backed TH1
  asset loading.
- Because Fossil page dispatch is page-name oriented, the stable live route
  shape should prefer flat page names such as:
  - `/agent-api-v1-sessions`
  - `/agent-api-v1-session`
  - `/agent-api-v1-chat`
  rather than slash-nested names like `/agent-api/v1/...`.
- Fossil now exposes explicit v1 capability and unsupported-operation
  responses for:
  - `capabilities`
  - `requests/active`
  - `request/cancel`
  - `session/delete`
  - `session/fork`
- Goose has a first compatibility layer in `ui/desktop/src/fossilApi.ts`.
- Goose desktop now has an initial backend namespace split under:
  - `ui/desktop/src/backends/fossil/`
  - `ui/desktop/src/backends/shared/`
- Goose can now treat Fossil as an alternate backend for core session/chat
  flows, but only partially.

What is not yet true:

- the Fossil API contract is not yet complete enough to cover the full Goose
  desktop lifecycle
- Fossil does not yet emit a Goose-grade streaming event API
- Goose still mixes backend-specific adaptation logic in the desktop app layer
- the repo-level architecture and docs still describe older, repo-local shapes
- licensing policy for cross-project sharing is still implicit

## Architectural Direction

The target shape is:

- Fossil C:
  repository-native state, storage, retrieval, indexing, process control,
  backend execution, permissions, and low-level tool primitives
- Fossil TH1:
  orchestration policy, role behavior, prompt assembly, workflow configuration,
  lightweight task composition
- Goose:
  agent/session management, desktop and richer dynamic UI, multi-request
  orchestration shell, higher-level human interaction and developer workflow UX
- Shared boundary:
  versioned HTTP+JSON+SSE contract, documented schemas, shared test vectors,
  and a compatibility matrix

That means code should be shared through contract and spec first, not by
copying runtime internals back and forth.

## License Strategy

This section is an engineering policy, not legal advice.

### Current Licenses

- Fossil fork: Simplified BSD / 2-Clause BSD
- Goose fork: Apache-2.0

### Practical Policy

Use these rules unless you later choose to relicense your own forks more
explicitly:

1. Do not directly copy Apache-2.0 Goose source files into Fossil.
   - Fossil should reimplement behavior from the contract and architecture,
     not import Goose code.
2. Do not directly copy Fossil source files into Goose unless the copied files
   remain clearly attributed and the BSD notice is preserved.
   - Even then, prefer reimplementation over file transfer.
3. Treat these items as safe shared artifacts:
   - API schemas
   - JSON examples
   - SSE event examples
   - protocol docs
   - generated fixtures and test vectors
   - architecture notes
4. If you need shared code, create a new explicitly-owned layer for it in your
   forks and license that layer intentionally rather than inheriting by
   accident.
5. Mark any intentionally imported third-party-derived code in a dedicated
   subtree such as:
   - `third_party/fossil-derived/`
   - `third_party/goose-derived/`
   with preserved notices.

### Immediate Deliverable

Add a short `CROSS_PROJECT_LICENSE_POLICY.md` note in both repositories so the
contributor rule is explicit before more integration work lands.

## Phase 1: Finish The Contract

Objective:

- make Fossil's API complete enough that Goose can treat it as a first-class
  backend, not a thin special case

Deliverables:

1. complete session lifecycle endpoints in Fossil:
   - create
   - get
   - list
   - rename
   - delete or explicit "unsupported" response
   - fork or explicit "unsupported" response
2. normalize event endpoints:
   - polling list
   - true streaming SSE endpoint
   - request cancellation endpoint or explicit "unsupported" response
   - active request discovery endpoint
3. add stable payload fields:
   - `api_version`
   - `ok`
   - `error`
   - `request_id`
   - `created_at`
   - `updated_at`
   - `message_count`
   - `capabilities`
4. document unsupported Goose features explicitly instead of silently dropping
   behavior

Verification:

- Fossil tests cover all v1 endpoints
- Goose can open, rename, chat, stream, and cancel through the same contract

## Phase 2: Normalize The Event Model

Objective:

- replace Fossil's stored-row-centric event view with a contract-friendly event
  model that maps cleanly to Goose UI behavior

Deliverables:

1. define canonical event kinds:
   - `message`
   - `progress`
   - `thinking`
   - `tool_request`
   - `tool_result`
   - `notification`
   - `error`
   - `finish`
2. preserve repository storage details internally while exposing normalized API
   events externally
3. add explicit request lifecycle concepts:
   - request started
   - request active
   - request cancelled
   - request finished
4. publish example event transcripts in docs

Verification:

- Goose no longer needs ad hoc event translation beyond a thin backend adapter

## Phase 3: Reorganize Goose Around Backend Modules

Objective:

- make Goose's repo and UI code reflect the same separation discipline now used
  in Fossil

Target desktop shape:

- `ui/desktop/src/backends/goose/`
- `ui/desktop/src/backends/fossil/`
- `ui/desktop/src/backends/shared/`

Suggested move plan:

1. move `fossilApi.ts` into a backend namespace:
   - `ui/desktop/src/backends/fossil/client.ts`
   - leave `fossilApi.ts` as a transitional shim only while imports are moved
2. move session/event compatibility logic out of generic hooks where possible:
   - Fossil session adapter
   - Fossil event adapter
   - shared backend selection helper
3. reduce `useChatStream.ts` to backend-independent chat/session state logic
4. isolate feature gating and unsupported actions in backend capability
   descriptors

Verification:

- backend-specific code becomes grep-able and isolated
- Fossil adapter changes do not sprawl across unrelated desktop components

## Phase 4: Expand Test Coverage

Objective:

- make the integration test-backed on both sides

Fossil tests:

1. extend `tst/agent.test` for:
   - `/agent-api-v1-session-create`
   - `/agent-api-v1-session`
   - `/agent-api-v1-sessions`
   - `/agent-api-v1-session-name`
   - `/agent-api-v1-chat`
   - `/agent-api-v1-events`
2. add SSE-specific tests once the streaming endpoint is stabilized
3. add compatibility tests for error envelopes and unsupported operations

Goose tests:

1. add unit tests for Fossil adapter normalization:
   - session list mapping
   - session detail mapping
   - event mapping
   - fallback chat path
2. add hook-level tests for:
   - Fossil backend detection
   - request submit
   - cancellation
   - unsupported feature handling
3. add one focused desktop integration path for Fossil-backed chat

Verification:

- both repos have explicit regression coverage for the integration contract

## Phase 5: Documentation Reconciliation

Objective:

- make the new ideology visible in the top-level docs instead of burying it in
  implementation notes

Fossil docs to update:

1. `README.md`
2. `doc/ai/TH1_ORCHESTRATION.md`
3. `doc/ai/AGENT_EVENTS.md`
4. new cross-project docs:
   - `doc/ai/FOSSIL_GOOSE_PHASE2_PLAN.md`
   - `doc/ai/CROSS_PROJECT_LICENSE_POLICY.md`

Goose docs to update:

1. `README.md`
2. `documentation/docs/goose-architecture/goose-architecture.md`
3. new docs:
   - `documentation/docs/experimental/fossil-integration.md`
   - `documentation/docs/experimental/cross-project-license-policy.md`

Documentation message to make explicit:

- Fossil is the repository-native execution and persistence substrate
- TH1 is the configurable orchestration layer inside Fossil
- Goose is the dynamic UI and higher-level agent shell
- the shared boundary is an API contract, not copied implementation

## Execution Order

Recommended order:

1. freeze the current v1 API with tests
2. add missing session lifecycle and streaming endpoints in Fossil
3. refactor Goose backend code into explicit backend namespaces
4. add Goose adapter tests
5. write cross-project license policy notes
6. update both README and architecture docs

## Exit Criteria

This phase is complete when:

- Goose can treat Fossil as a documented backend via a stable API
- Fossil exposes request-aware JSON/SSE semantics, not just stored chat rows
- both repos have explicit test coverage for the integration contract
- license boundaries are documented and contributor-safe
- Goose repo layout reflects backend modularity
- both projects' docs clearly describe the new overall architecture
