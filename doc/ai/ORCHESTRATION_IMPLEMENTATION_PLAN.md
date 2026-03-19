# AI Agent Orchestration Implementation Plan

Purpose: define one Fossil-native implementation plan for agent orchestration
by combining the most useful ideas from five references:

- `~/goose` for workflows, self-test, diagnostics, and explicit capabilities
- `~/gentle-ai` for inspect/apply/verify discipline, rollback, and dry-run UX
- OpenCode-oriented command and skill patterns for subtask execution,
  structured phase contracts, and async delegation
- `~/openclaw` for execution contracts, approval binding, boundary inventories,
  and runtime observability
- `~/Fabric` for artifact-backed prompt assets, guidance layering, and reusable
  context/session distinctions

This is not a plan to turn Fossil into Goose, `gentle-ai`, OpenCode,
`openclaw`, or `Fabric`. The goal is to absorb the useful patterns while
preserving Fossil's strengths:

- single-binary deployment
- repository-local state
- TH1 scripting
- explicit provenance
- offline/local-first operation

## Design Summary

Fossil should implement a **recipe-driven, TH1-orchestrated agent system**
with explicit capabilities, structured events, verification, and optional
background delegation.

In concrete terms:

- **Goose contributes** the workflow model:
  recipes, self-test flows, diagnostics bundles, capability registry, evals
- **`gentle-ai` contributes** the operational model:
  inspect/apply/verify, dry-run, rollback, config safety, modular provider
  policy
- **OpenCode contributes** the execution model:
  command-style phase entry points, structured sub-agent contracts, persistent
  artifacts, and async-first delegation
- **`openclaw` contributes** the contract model:
  explicit compatibility boundaries, approval binding, parseable machine
  outputs, and stronger runtime observability
- **`Fabric` contributes** the guidance model:
  named prompt assets, reusable contexts, task-oriented patterns, and
  artifact-backed override layers

The implementation center remains Fossil itself:

- **C core** handles process execution, DB access, vector search, and event
  persistence
- **TH1** handles orchestration, prompt assembly, response parsing, recipe
  control flow, and capability-aware policy logic
- **repository tables and versioned files** hold recipes, guidance, evals, and
  diagnostics artifacts

## Core Principles

### 1. Orchestration is explicit

Agent workflows should be defined as first-class recipes or plans, not hidden
inside one long prompt string.

### 2. Execution is capability-bound

Every agent-accessible action should be declared, inspectable, and policy-aware
before a session runs.

### 3. State is persisted and replayable

Sessions, events, recipe runs, eval results, and diagnostics should be stored
in repository-native form so failures can be replayed and inspected.

### 4. Setup is staged and verifiable

Any operation that changes agent configuration or repository AI state should
support inspect/apply/verify behavior, with rollback where Fossil mutates
config files.

### 5. Delegation is structured, not magical

Subtasks should run through explicit command/recipe contracts with clear
inputs, outputs, and artifact persistence.

### 6. Guidance is artifact-backed

Reusable AI guidance should increasingly live as inspectable repository
artifacts rather than opaque embedded strings or only user-local state.

### 7. Machine interfaces are contractual

JSON modes, approval prompts, and web/API execution surfaces should have stable
contracts that remain parseable and regression-testable as diagnostics grow.

## Source Contributions

### Goose

High-value ideas to adopt:

- recipe-oriented workflows
- first-person agent self-test
- diagnostics bundle export
- explicit capability registration
- scenario-based evals

Goose is most useful as the source of the **workflow and eval model**.

### `gentle-ai`

High-value ideas to adopt:

- dry-run before mutation
- staged execution and verification
- config backup and rollback
- modular adapter or provider policy boundaries
- strong operational reporting

`gentle-ai` is most useful as the source of the **safety and operability
model**.

### OpenCode

High-value ideas to adopt:

- command-style orchestration entry points
- phase-specific contracts like init, explore, spec, apply, verify, archive
- explicit sub-agent envelopes with status, artifacts, risks, and next actions
- async/background delegation for bounded subtasks
- persistent artifact lookups rather than relying on prompt memory

OpenCode is most useful as the source of the **execution contract and
delegation model**.

### `openclaw`

High-value ideas to adopt:

- approval binding to actual command argv, cwd, and environment
- boundary inventories enforced by tests
- explicit compatibility and partial-support documentation
- strict machine-readable stdout behavior for JSON commands
- better runtime observability around runs, logs, and tool activity

`openclaw` is most useful as the source of the **contract, approval, and
observability model**.

### `Fabric`

High-value ideas to adopt:

- named prompt and strategy assets as files
- explicit separation of reusable context from session history
- inspectable prompt composition
- layered built-in and user-authored guidance
- task-oriented prompt entry points

`Fabric` is most useful as the source of the **guidance and reusable prompt
asset model**.

## Fossil-Native Architecture

### C Core Responsibilities

- low-level provider execution
- config resolution
- provider/model validation
- repository and checkout inspection
- vector retrieval and note graph operations
- structured event persistence
- diagnostics export packaging
- verification helpers and capability registry plumbing
- approval binding and execution-contract enforcement
- guidance artifact lookup and provenance capture

### TH1 Responsibilities

- recipe interpretation
- orchestration control flow
- phase transitions
- prompt construction
- response parsing
- retry and fallback logic
- capability-aware gating
- subtask launch policy
- run summaries
- guidance composition from artifact-backed sources
- phase-to-artifact provenance recording

### Persistent Objects

Fossil should add or formalize storage for:

- agent recipes
- recipe runs
- recipe phases
- guidance artifacts and guidance layers
- eval runs and eval cases
- capability declarations
- diagnostics artifacts or export manifests
- background task records
- repository guidance and policy sources

These can be backed by repository tables, versioned files, unversioned content,
or a hybrid model depending on the artifact type.

## Orchestration Model

### Recipe Layer

Borrowing from Goose, Fossil should define a first-class recipe format for
repeatable agent workflows.

Suggested fields:

- `name`
- `title`
- `description`
- `parameters`
- `instructions`
- `guidance_refs`
- `phases`
- `allowed_capabilities`
- `model_policy`
- `success_criteria`
- `eval_hooks`

Execution should compile the recipe into TH1 orchestration calls rather than
interpret arbitrary YAML throughout the C layer.

Suggested surfaces:

- `fossil agent recipe list`
- `fossil agent recipe show NAME`
- `fossil agent recipe run NAME`
- `/agent-recipes`

### Phase Contracts

Borrowing from OpenCode, each major orchestration phase should have a clear
contract rather than one generic "run agent" path.

Initial phases:

- `init`
- `explore`
- `spec`
- `design`
- `tasks`
- `apply`
- `verify`
- `archive`
- `selftest`
- `diagnostics`

Each phase should return a structured envelope such as:

- `status`
- `executive_summary`
- `detailed_report`
- `artifacts`
- `risks`
- `next_recommended`

This keeps both CLI and `/agentui` behavior predictable.

### Capability Registry

Borrowing from Goose, but grounded in Fossil policy, available tools should be
declared explicitly.

Suggested capability fields:

- `name`
- `kind`
- `description`
- `trust_level`
- `requires_write`
- `requires_network`
- `requires_confirmation`
- `phase_scope`
- `provider_scope`

Suggested surfaces:

- `fossil agent capabilities`
- `/agent-capabilities`
- capability summary embedded in `/agent-config`

### Structured Event Stream

The event model should become the common runtime layer for:

- user prompt
- orchestration progress
- capability use
- subtask launch
- provider output
- final reply
- errors
- verification results
- promotion and materialization decisions
- guidance-source composition metadata

This gives Fossil one inspectable execution history for both chat and recipes.

## Verification And Safety Model

### Inspect / Apply / Verify

Borrowing directly from `gentle-ai`, any setup or mutation path should follow:

1. inspect current state
2. build plan
3. optionally render dry-run
4. apply changes
5. verify outcome
6. rollback if verification fails and a rollback target exists

This applies to:

- AI config bootstrapping
- future provider installation helpers
- recipe-managed repo guidance setup
- capability policy changes
- artifact materialization and promotion paths

### `fossil agent verify`

This should become the operational verification entry point.

Suggested checks:

- effective config source resolution
- provider/model pair validation
- backend command availability
- embedding availability
- event persistence health
- fake-backend smoke run
- recipe runtime sanity
- capability registry consistency

Outputs should be:

- human-readable summary
- JSON for CI
- optionally stored repo-local run rows

For JSON modes, Fossil should adopt a hard contract:

- stdout remains parseable JSON
- warnings move to stderr or structured JSON fields
- machine mode output does not gain prose prefixes over time

### Diagnostics Bundle

Borrowing from Goose and reinforced by `gentle-ai`'s operational posture,
Fossil should add:

- `fossil agent diagnostics`

Suggested contents:

- effective config summary
- selected session and event rows
- relevant AI tables
- verification results
- provider and platform info
- redacted command metadata
- recipe run summaries

Default behavior must favor safe redaction.

## Execution Contract Model

### Approval Binding

Borrowing from `openclaw`, any agent-triggered command approval should bind to
the actual execution contract, not only a human-readable command string.

The binding should include:

- canonical argv
- working directory
- relevant environment overrides
- run or session identity

This matters before Fossil expands write-capable orchestration or
materialization flows.

### Boundary Inventories

Borrowing from `openclaw`, Fossil should maintain small inventory-style checks
for critical AI boundaries, such as:

- AI web routes
- AI CLI entry points
- recipe and TH1 orchestration hooks
- artifact materialization paths
- JSON contract surfaces

The goal is not perfection on day one. The goal is visible, regression-tested
boundaries.

## Delegation Model

### Bounded Background Tasks

Borrowing from OpenCode's background-agent pattern, Fossil should support
optional async delegation for narrow subtasks.

Examples:

- summarize retrieved notes
- derive candidate tasks from a design doc
- run a repo exploration recipe
- produce alternate rewrite proposals

Delegation should be:

- explicitly requested by the active recipe or TH1 orchestration
- bounded by allowed capabilities
- persisted as a background task record
- observable from CLI and UI

Suggested surfaces:

- `fossil agent task list`
- `fossil agent task show ID`
- `fossil agent task cancel ID`

### Persistence Contract

Borrowing from OpenCode's artifact discipline, delegated work should read from
persistent artifacts and write back structured artifacts rather than rely on
conversation memory alone.

In Fossil terms, delegated tasks should consume and produce:

- `ai_note`
- `ai_context`
- recipe artifacts
- task/eval/diagnostic rows
- event metadata

This keeps delegation replayable and testable.

## Guidance And Policy Model

Repositories should be able to provide stable instructions to orchestration
without burying them in user-local config.

Suggested sources:

- versioned guidance file such as `.fossil-agent.md`
- wiki-backed guidance page
- repo-local recipe files
- guidance artifacts under `doc/ai/guidance/` or similar
- built-in safety policy

Precedence should be explicit:

1. built-in safety policy
2. repo policy and guidance
3. recipe instructions
4. user prompt

The source of each injected context fragment should be persisted.

Borrowing from `Fabric`, Fossil should also keep a clear distinction between:

- reusable guidance artifacts
- session or run history
- retrieved knowledge notes

That distinction should be visible in both UI and saved run details.

## Testing And Evals

### Self-Test

Borrowing from Goose, Fossil should treat self-test as an orchestration feature
instead of only a schema smoke path.

Suggested modes:

- `fossil ai selftest` remains schema and retrieval focused
- `fossil agent selftest` becomes orchestration-focused

Suggested phases:

- config
- capabilities
- retrieval
- recipe execution
- event persistence
- delegation
- `/agentui` JSON flows
- diagnostics export

### Scenario Evals

Borrowing from Goose and OpenCode's structured phase model, Fossil should add
scenario-based evals for orchestration behavior.

Examples:

- recipe run with invalid provider config
- background subtask failure with parent run recovery
- verify phase detecting missing embeddings
- guidance precedence correctness
- capability denial behavior
- JSON stdout contract preservation under warning conditions
- approval binding mismatch behavior
- promotion and materialization transition correctness

The important point is not model scoring. It is replayable behavioral
regression coverage.

## Phased Implementation Plan

### Phase 0: Consolidate Current Runtime

Objective:

- stabilize the existing agent runtime before adding recipe complexity

Deliverables:

- finish provider-aware validation and capability reporting
- add `fossil agent verify`
- extend `/agent-config` with verification summary
- keep TH1 orchestration as the control plane for chat

Exit criteria:

- one command can explain what backend will run and whether it is operational

### Phase 1: Recipes And Phase Contracts

Objective:

- introduce repeatable orchestration units

Deliverables:

- recipe schema and storage model
- CLI for list/show/run
- TH1 recipe runner
- structured phase envelopes
- minimal built-in recipes: `selftest`, `summarize-context`, `review-change`

Exit criteria:

- a repository can define and run repeatable agent workflows without editing
  prompt strings

### Phase 2: Capability Registry And Guidance Sources

Objective:

- replace implicit tool exposure with explicit policy

Deliverables:

- capability registry
- repo guidance source support
- context precedence rules
- capability-aware recipe validation

Exit criteria:

- the active tool and policy surface is inspectable before execution

### Phase 3: Diagnostics, Evals, And Verification Storage

Objective:

- make failures supportable and replayable

Deliverables:

- diagnostics bundle export
- orchestration self-test
- eval run storage
- machine-readable verify output

Exit criteria:

- a failed run can be diagnosed from one exported artifact and one stored eval
  record

### Phase 4: Background Delegation

Objective:

- add bounded async subtask execution where it improves throughput

Deliverables:

- task registry and persistence
- task list/show/cancel surfaces
- delegation events in the runtime stream
- recipe opt-in for background subtasks

Exit criteria:

- delegated work is visible, bounded, persistent, and recoverable

### Phase 5: Mutation Safety For Config-Editing Operations

Objective:

- make direct config mutation safe if Fossil starts doing more of it

Deliverables:

- config snapshots
- rollback on verify failure
- dry-run for bootstrap and install helpers

Exit criteria:

- Fossil can mutate AI-related config without leaving users in a half-broken
  state

## Recommended Build Order

The right order is:

1. `fossil agent verify`
2. verification summary in `/agent-config`
3. recipe schema and TH1 runner
4. capability registry
5. orchestration self-test and diagnostics export
6. repo guidance support
7. background delegation
8. config snapshot and rollback for mutation paths

This order front-loads clarity and testability before adding delegation and
more automation.

## Non-Goals

This plan does not require Fossil to become:

- a general editor integration framework
- a persona and preset product
- a hosted agent platform
- an MCP-first architecture
- a multi-process orchestration server

Fossil should stay repository-native and local-first. The orchestration system
should make Fossil's existing AI and provenance model more reliable and more
usable, not broaden Fossil into a generic agent manager.
