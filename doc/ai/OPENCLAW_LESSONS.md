# Lessons From `~/openclaw`

Purpose: capture what Fossil can learn from the separate `~/openclaw` project
without copying its product scope, channel surface, or plugin ecosystem.

## Summary

`openclaw` is not an SCM and it is not a repository knowledge system. It is a
large personal-assistant platform with a gateway, channel integrations,
companion apps, onboarding, plugins, and aggressive operational hardening.

The useful lessons for Fossil are not "add more channels" or "become a
consumer assistant." The useful lessons are structural:

- make runtime contracts explicit and machine-checked
- keep extension and provider boundaries visible and enforced by tests
- preserve parseable CLI output even when diagnostics are present
- make approvals bind to the actual command invocation, not a vague prompt
- improve UI observability around tool use, runs, and logs
- separate control-plane surfaces from product-facing interaction surfaces

Fossil should not copy OpenClaw's messaging-first product shape, plugin
marketplace ambitions, or companion-app sprawl.

## What `openclaw` Does Well

Several patterns in `openclaw` are worth reusing conceptually:

- explicit bridge and protocol documentation with a compatibility matrix
- contract fixtures for risky execution behavior
- boundary inventories enforced by tests rather than tribal knowledge
- careful JSON stdout discipline for automation-facing commands
- durable operational logging and debug surfaces
- a strong distinction between core runtime, extension surfaces, and UI shells

These are not narrow "AI features." They are engineering controls for a
growing AI system.

## Current Fossil Position

Fossil already has a stronger repository-native AI foundation than OpenClaw in
some areas:

- repo-backed AI state, retrieval, review, and chat persistence
- a built-in verification and diagnostics surface
- a capability registry, recipe registry, phase contracts, and run ledger
- a knowledge browser and pool-oriented UI
- explicit provider and model handling
- Tcl regression coverage for CLI and web flows

What Fossil does not yet have to the same degree is OpenClaw's contract
discipline around boundaries, approvals, and operator-facing runtime
observability.

## What Fossil Should Learn

### 1. Document compatibility and partial-support surfaces explicitly

One of OpenClaw's strongest habits is saying exactly what a bridge or protocol
does and does not implement. Its ACP bridge doc uses a compatibility matrix and
known-limitations section instead of vague claims.

Fossil should apply the same pattern to:

- recipe phases
- `/agentui` event and history semantics
- knowledge materialization status
- saved-run replay semantics
- any future ACP/MCP/editor bridge

The lesson is simple: partial support is fine if it is stated precisely.

### 2. Treat boundaries as inventory, not aspiration

OpenClaw has tests that enumerate architectural smells and import-boundary
violations, then compares them against checked-in inventories or zero-length
expectations.

Fossil should adopt the same idea for its AI surfaces. Good candidates:

- agent/provider policy concentrated in `src/agent.c`
- TH1 orchestration entry points and which commands can invoke them
- pages and JSON endpoints that expose AI state
- future artifact materialization paths under `knowledge/`

The point is not to eliminate every exception immediately. The point is to
make exceptions visible and regression-testable.

### 3. Be stricter about machine-readable CLI contracts

OpenClaw tests that `--json` output remains parseable even when legacy
preflight or doctor-style warnings exist.

Fossil now has more JSON-bearing AI commands than before:

- `fossil agent verify --json`
- `fossil agent diagnostics --json`
- `fossil agent recipe run ... --json`
- `/agent-config`, `/agent-history`, `/agent-events`, and related endpoints

Fossil should explicitly guarantee:

- stdout stays clean JSON in JSON modes
- warnings move to stderr or structured fields
- wrappers do not prepend human text in machine modes

This is especially important if diagnostics, promotion notices, or
materialization hints grow more verbose.

### 4. Tighten approval semantics for agent execution

OpenClaw does a strong job of binding approvals to the actual command argv and
environment, with contract fixtures for mismatches and wrapper edge cases.

Fossil should learn from that before expanding agent write or shell
capabilities. If a recipe or future sub-agent phase asks to run a command, the
approval should bind to:

- canonical argv
- working directory
- relevant environment overrides
- session or run identity

That matters because Fossil is moving toward more persistent orchestration and
artifact materialization. Weak approval semantics become a real security hole
once agent actions are durable and repeatable.

### 5. Make operator observability first-class

OpenClaw invests in logging and runtime inspection as product features, not
afterthoughts. Even its UI-facing tool summaries are normalized through a
shared display configuration.

Fossil should take the same direction for AI operations:

- clearer run timelines for recipe and chat execution
- browseable saved runs in the web UI, not only CLI
- better retrieval history browsing by note and by run
- explicit display of tool or command actions where applicable
- durable logs or export bundles for debugging AI behavior

Fossil already has the raw ingredients: event persistence, diagnostics, and a
saved run ledger. The next step is making those inspectable from the web shell.

### 6. Keep core and extensions separate on purpose

OpenClaw is opinionated about what belongs in core versus plugins or external
bridges. That keeps the main runtime smaller and reduces accidental coupling.

Fossil should apply the same discipline to future AI growth:

- keep repository-native knowledge and orchestration in core
- keep provider wrappers and helper scripts outside the core binary when
  possible
- avoid turning core AI into a general plugin marketplace
- keep external bridges like MCP/ACP or editor adapters as explicit edges,
  not implicit assumptions baked through the UI

This matters because Fossil's advantage is still single-binary clarity, not
feature sprawl.

### 7. Use executable policy tests for high-risk paths

OpenClaw goes beyond unit tests and treats some security guarantees as contract
or model surfaces. Fossil does not need to adopt the same TLA+ workflow to
benefit from the underlying idea.

Fossil should add sharper policy tests around:

- command approval binding
- retrieval-to-promotion transitions
- materialization eligibility and artifact status changes
- repo config vs checkout config precedence
- saved-run replay and redaction behavior

High-risk behavior should have explicit "this must never regress" tests, not
only broad end-to-end coverage.

## What Fossil Should Not Copy

Fossil should not import the following OpenClaw concepts wholesale:

- a multi-channel assistant product identity
- companion-app sprawl across desktop and mobile platforms
- a large plugin marketplace as a default AI strategy
- a gateway-centered architecture that weakens Fossil's repository-first model
- onboarding flows that hide repository and execution details too aggressively

Those choices make sense for OpenClaw because it is building a personal
assistant platform. Fossil should remain a repository tool with AI-enhanced
knowledge and orchestration.

## Concrete Fossil Roadmap

This roadmap is intentionally narrow and Fossil-native.

### Three Small Changes

#### 1. Add explicit JSON contract tests for all AI JSON commands

For every AI command with `--json`, add tests that assert stdout is parseable
JSON even when warnings or preflight conditions exist.

Targets:

- `verify`
- `diagnostics`
- `recipe run`
- future materialization and run-export commands

#### 2. Add a web view for saved runs and run artifacts

Fossil already persists runs. The missing part is a first-class browser page
for them.

The page should show:

- run type
- phase/status
- timestamps
- linked retrievals or notes
- artifacts and summaries

#### 3. Add explicit approval-binding tests before expanding execution

Before adding stronger shell or artifact-writing capabilities, introduce
contract-style tests for:

- argv matching
- cwd binding
- environment binding
- run/session scoping

### Three Medium Refactors

#### 1. Split UI display metadata from execution metadata

OpenClaw normalizes tool-display logic separately from execution internals.
Fossil should do something similar for:

- recipe and phase labels
- run status labels
- retrieval and promotion badges
- artifact materialization display

This will keep the web UI from baking policy strings directly into rendering
logic.

#### 2. Create an AI boundary inventory

Add a small generated inventory or lint-like report that tracks:

- AI web routes
- AI CLI commands
- TH1 orchestration entry points
- artifact-backed knowledge paths

Then regression-test that inventory.

#### 3. Add a compatibility matrix doc for the AI shell

Create one document that says exactly what the current AI stack supports and
what is partial or missing across:

- chat
- retrieval
- recipes
- saved runs
- materialized artifacts
- future external bridges

## One Thing To Avoid

Do not let Fossil's AI work drift into a gateway-plus-plugins platform whose
main unit of design is "integrations." OpenClaw is strongest where it manages
that complexity deliberately, but Fossil's comparative advantage is much
smaller and cleaner:

- repository-native state
- single-binary deployment
- explicit provenance
- inspectable local workflows

The right lesson from OpenClaw is "enforce contracts as the system grows," not
"grow into OpenClaw."
