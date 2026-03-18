# Lessons From `~/gentle-ai`

Purpose: capture what Fossil can learn from the separate `~/gentle-ai` project
without importing its full product shape or drifting away from Fossil's core
goals.

## Summary

`gentle-ai` is not another SCM and it is not a drop-in architecture for
Fossil. It is a Go-based configurator for external AI coding agents. Its value
to Fossil is therefore not "copy its feature list." The useful lessons are
operational:

- make planned changes inspectable before execution
- stage risky changes and make rollback explicit
- verify post-apply state with machine-readable checks
- keep provider integration logic modular as the AI surface grows
- document the AI subsystem as a set of distinct responsibilities

Fossil should not copy the full skills, persona, preset, or ecosystem-manager
layer. That belongs to `gentle-ai`'s product scope, not Fossil's.

## What `gentle-ai` Does Well

Several patterns in `gentle-ai` are worth reusing conceptually:

- explicit subsystem boundaries: planner, pipeline, backup, verify, assets,
  and per-agent adapters
- staged execution with rollback rather than one opaque mutation path
- dry-run output that tells the operator what would happen before changes are
  applied
- first-class verification after changes are made
- backups taken before config mutation
- strong test posture around adapter behavior and rendered output

Those are not "AI features" in the narrow sense. They are reliability features
for AI-related behavior.

## Current Fossil Position

Fossil already has more AI structure than an outside comparison might suggest.

Implemented already:

- repo-backed AI feature gating and schema setup in `src/ai.c`
- a built-in `fossil ai selftest` smoke path
- layered AI config resolution in `src/agent.c`
- effective config and capability reporting for `/agentui`
- wrapper-based chat backends for Ollama and Codex
- session-backed chat persistence and event recording
- Tcl regression coverage for AI CLI and web flows

This means Fossil does not need a fresh AI architecture. It needs sharper
operational boundaries around the AI behavior that already exists.

## What Fossil Should Learn

### 1. Treat AI setup as a staged operation

Today Fossil has working runtime config resolution and install-time helper
scripts, but the setup path is still closer to "do the thing" than "plan,
apply, verify."

The lesson from `gentle-ai` is to treat user-facing AI setup as a staged
operation with three clear phases:

- inspect current state
- describe intended changes
- apply and then verify

This is especially relevant for:

- installing or refreshing wrapper scripts
- writing starter config files
- adding future provider integrations
- making repo or user config changes from the CLI

### 2. Make rollback explicit where Fossil mutates config

If Fossil gains commands that edit user AI config directly, it should snapshot
the original files first. This matters more than it might in a typical app
because AI config failures often present as vague backend errors long after the
root cause was introduced.

The target behavior is simple:

- snapshot the affected files
- apply the change
- if verification fails, restore the prior version

### 3. Add a dedicated verification command

Fossil already has `fossil ai selftest`, but that command mainly exercises the
repository-side schema and retrieval loop. It is not a full operational check
of the configured agent runtime.

Fossil should add a separate verification surface focused on runtime behavior:

- resolved config source and effective provider/model
- wrapper or backend executability
- provider/model validation
- embedding availability
- fake-backend smoke testing where possible

This should be usable from CI and local shell scripts.

### 4. Keep provider integration modular

`gentle-ai` benefits from a visible adapter boundary per agent. Fossil does not
need a large Go-style adapter tree, but the underlying idea is sound:

- provider metadata should be defined in one place
- capability flags should be provider-scoped
- model suggestions should be provider-scoped
- validation should be provider-aware
- streaming support should be treated as a provider capability, not a generic
  property of "AI"

Fossil has already started this work with explicit provider names and
capability fields. The next step is to make that logic easier to extend
without concentrating all policy inside one large `src/agent.c`.

### 5. Prefer inspectable output over implicit behavior

One of the strongest habits in `gentle-ai` is making execution intent visible:

- dry-run output
- platform decision output
- dependency reporting
- post-apply verification results

Fossil should take the same approach for AI operations. A developer should be
able to ask:

- what config is active?
- what provider/model will run?
- what files would an install/bootstrap command touch?
- what failed during verification?

and get structured, stable answers.

## What Fossil Should Not Copy

Fossil should not import the following `gentle-ai` concepts wholesale:

- a persona/preset/skills product layer
- a general AI ecosystem configurator identity
- complex editor-specific configuration management as a primary feature
- growth in AI surface area that is not tightly connected to Fossil's
  repository, sync, or web capabilities

Those features make sense for `gentle-ai` because its product is "configure
your coding agents." Fossil's product is still "version control, collaboration,
and repository-native tooling."

## Concrete Fossil Roadmap

This roadmap is intentionally narrow. The goal is to improve reliability and
operability of Fossil's AI features without turning Fossil into a general agent
manager.

### Three Small Changes

#### 1. Add `fossil agent verify`

Introduce a command dedicated to operational verification of AI runtime setup.

Suggested checks:

- effective config source resolution
- provider/model pair validation
- command wrapper presence and executability
- embedding availability status
- optional fake-backend chat smoke test

Expected outcome:

- faster diagnosis than "open `/agentui` and see what happens"
- a stable command suitable for CI and regression tests

#### 2. Add dry-run output for AI bootstrap or install helpers

Where Fossil writes AI config skeletons or agent wrapper files, add a mode that
reports intended file changes without applying them.

Suggested output:

- destination paths
- chosen provider/model defaults
- whether files already exist
- whether a file would be created, replaced, or left untouched

Expected outcome:

- safer first-use experience
- easier documentation and testing

#### 3. Add a verification summary endpoint for `/agentui`

Fossil already exposes effective config JSON. Extend that surface with a small
verification summary so the UI and tests can distinguish:

- config is resolvable
- backend command exists
- provider/model pair is valid
- embeddings are available

Expected outcome:

- fewer UI-side guesses
- clearer failures in both browser and tests

### Three Medium Refactors

#### 1. Split provider policy out of `src/agent.c`

Move provider-specific choices and validation into a dedicated module or at
least a clearly separated table-driven section.

This should cover:

- known providers
- model suggestions
- capability flags
- provider/model validation rules
- embedding support rules

Expected outcome:

- simpler extension path for new providers
- reduced risk of accidental policy regressions

#### 2. Add config snapshot and restore support for user AI config edits

If Fossil starts editing user AI config directly, add a small backup/restore
path before mutation.

Scope:

- snapshot targeted config files
- apply mutation
- run verification
- restore on failed verification

Expected outcome:

- safer automation
- fewer "broken local setup" failure modes

#### 3. Make AI setup a staged inspect/apply/verify flow

Instead of one monolithic setup path, make AI bootstrap operations follow a
simple lifecycle:

1. inspect current state
2. build a plan
3. optionally render the plan
4. apply the plan
5. verify results

Expected outcome:

- clearer command behavior
- easier testing
- easier future extension to more providers

### One Thing To Avoid

Do not turn Fossil into a general external-agent configuration framework.

That would likely produce:

- a large maintenance surface
- editor- and vendor-specific churn
- weaker focus on repository-native AI features

Fossil should remain opinionated and narrow: repository-aware AI, provenance,
retrieval, chat, evaluation, and tightly-scoped local agent integration.

## Recommended Order

The sequence below keeps the work incremental:

1. add `fossil agent verify`
2. extend tests around verification and fake backends
3. add dry-run reporting for AI bootstrap/install behavior
4. factor provider policy into a dedicated module or table-driven section
5. add snapshot/restore only if Fossil begins mutating user AI config directly

This order improves operator clarity first, then structural cleanliness, then
mutation safety when it becomes necessary.
