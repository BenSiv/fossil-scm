# Lessons From `~/Fabric`

Purpose: capture what Fossil can learn from the separate `~/Fabric` project
without turning Fossil into a prompt framework or inheriting Fabric's full
provider and pattern ecosystem.

## Summary

`Fabric` is most useful to Fossil as an example of treating prompts,
contexts, sessions, and strategies as human-manageable artifacts.

Its strongest reusable ideas are:

- store reusable prompt assets as named files rather than burying them in code
- separate reusable context from conversation history
- make prompt composition understandable to users
- allow local overrides and user-authored pattern extensions
- keep model/vendor selection modular

Fossil should not copy Fabric's large prompt-library product identity or its
"download a prompt ecosystem" posture. The durable lesson is about explicit,
inspectable AI guidance assets.

## What `Fabric` Does Well

Several patterns in `Fabric` are worth reusing conceptually:

- prompt patterns live as concrete files under `data/patterns/`
- strategies are first-class files under `data/strategies/`
- contexts and sessions are distinct concepts with distinct storage
- custom patterns can override or extend built-ins from a local directory
- prompt assembly is simple enough to explain in docs and code
- provider and setup handling is modular rather than hard-coded into one path

These are valuable because they make AI behavior easier to inspect, reuse, and
evolve.

## Current Fossil Position

Fossil has already moved in a related direction:

- TH1-backed recipes exist as a reusable orchestration layer
- repo-backed knowledge, retrieval, and review state are persisted
- saved runs, diagnostics, and recipe execution are now durable objects
- storage policy docs now distinguish runtime/index state from durable
  artifact-backed text
- the knowledge browser is starting to expose the pool as a first-class system

What Fossil does not yet have to the same degree is a clean artifact model for
reusable AI guidance itself. Some guidance exists in TH1 or C tables, but it is
not yet as obviously inspectable and overridable as Fabric's pattern files.

## What Fossil Should Learn

### 1. Treat reusable AI guidance as repository artifacts

Fabric's biggest strength is that its core prompting assets are files. Users
can read them, compare them, edit them, and version them.

Fossil should apply that lesson to:

- recipe guidance
- system prompt fragments
- phase instructions
- evaluation fixtures
- promotion/materialization policies where text is involved

This lines up with Fossil's current storage direction: durable textual
knowledge should increasingly live as repository artifacts, with SQLite acting
as runtime state, index, and audit layer.

### 2. Keep context separate from session history

Fabric makes a useful distinction:

- context is reusable injected guidance
- session is accumulated conversation history

Fossil already has ingredients for this distinction, but the UX still leans
heavily on "chat session" as the main object. The knowledge system will be
clearer if Fossil is more explicit about:

- reusable repository guidance
- transient run/session state
- durable knowledge notes and materialized artifacts

That separation will matter more as the processing loop promotes notes and
recipes become richer.

### 3. Make composition inspectable

Fabric's composition model is easy to explain: context + pattern + session
history become the effective prompt.

Fossil should aim for the same clarity. For any recipe or chat run, operators
should be able to inspect:

- selected recipe
- applied guidance/context artifacts
- retrieval inputs
- phase contract
- effective provider/model

This is especially important now that Fossil has:

- recipes
- phases
- capability declarations
- retrieval-backed runs
- durable run logs

Without clear composition, the system becomes harder to trust.

### 4. Allow local overrides without losing core defaults

Fabric supports custom patterns alongside the built-in set. That is a useful
shape even if Fossil should be stricter about provenance.

Fossil should consider a layered guidance model such as:

- built-in recipes and guidance shipped with Fossil
- repo-versioned guidance under `knowledge/` or `doc/ai/`
- checkout-local or user-local overrides for experimentation

The key lesson is not "let everything be overridden." The key lesson is to
make the override order explicit and inspectable.

### 5. Keep prompt strategy separate from provider selection

Fabric has both strategies and vendors as distinct concerns. That is a good
discipline.

Fossil should preserve the same separation:

- provider/model controls runtime backend behavior
- recipes and phase guidance control task behavior
- retrieval and note tiers control knowledge selection

If those layers are mixed too early, extending the system becomes brittle.

### 6. Prefer artifact-backed reuse over hidden string blobs

Fabric's patterns are crude in places, but they are visible. That matters more
than elegance.

For Fossil, this suggests:

- move more recipe guidance into named files or versioned artifacts
- link saved runs to the guidance artifacts they used
- expose artifact provenance in the knowledge browser
- avoid burying important orchestration text inside opaque C string literals

This would fit well with the current plan to materialize more durable knowledge
into repository content.

### 7. Organize by real tasks, not only by technical primitives

Fabric's pattern system is organized around what the user wants to do. That is
useful.

Fossil's AI shell should increasingly expose task-oriented entry points such
as:

- summarize repository context
- review a change
- process and promote notes
- explain a retrieval set
- materialize a draft knowledge artifact

The underlying capabilities and phases still matter, but the user-facing layer
should be organized around real repository tasks.

## What Fossil Should Not Copy

Fossil should not import the following Fabric concepts wholesale:

- a giant general-purpose pattern library as a core product identity
- automatic internet download of prompt packs as a default requirement
- a user model centered on personal prompt collection rather than repository
  provenance
- prompt strategies that are disconnected from repository knowledge and review

Those choices fit Fabric because it is a prompt-and-pattern framework. Fossil
should remain repository-first.

## Concrete Fossil Roadmap

This roadmap is intentionally narrow and compatible with the current storage
model.

### Three Small Changes

#### 1. Move built-in recipe guidance into named artifacts

Keep the recipe registry in core, but move long guidance text into named files
or clearly versioned assets instead of embedding everything inline.

Expected outcome:

- easier review
- easier diffing
- easier future repo-level override support

#### 2. Expose effective guidance composition in run details

For recipe runs and chat runs, show which guidance sources were applied:

- recipe name
- phase
- retrieval set
- guidance artifact names or refs

Expected outcome:

- better trust
- easier debugging
- cleaner provenance

#### 3. Add a distinction between reusable guidance and session history in the UI

The web UI should make it obvious whether the agent is using:

- saved session history
- reusable repo guidance
- retrieved knowledge notes

Expected outcome:

- clearer mental model for operators

### Three Medium Refactors

#### 1. Formalize a repository guidance directory

Define a conventional location for durable AI guidance artifacts, for example:

- `doc/ai/guidance/`
- `knowledge/recipes/`
- `knowledge/evals/`

This should align with the existing storage-policy docs rather than bypassing
them.

#### 2. Add guidance layering rules

Document and enforce a clear precedence order for:

- built-in guidance
- repo guidance
- checkout-local or user-local experimental guidance

This should parallel Fossil's existing config-resolution discipline.

#### 3. Link runs and notes back to guidance artifacts

Saved runs, promoted notes, and materialized artifacts should record which
guidance assets influenced them.

That gives Fossil a much stronger provenance story than a plain chat transcript.

## One Thing To Avoid

Do not let Fossil become "a huge prompt catalog with a repository attached."
Fabric is strongest where it helps people manage prompt assets, but Fossil's
advantage is different:

- repository-native history
- structured knowledge processing
- durable provenance
- inspectable orchestration

The right lesson from Fabric is "make reusable AI guidance artifact-backed and
inspectable," not "turn Fossil into Fabric."
