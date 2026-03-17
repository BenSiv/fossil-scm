# Goose-Inspired Improvements Plan For Fossil

Scope: identify high-value ideas from the `~/goose` repository that can improve
Fossil's agent system without compromising Fossil's strengths: repository-local
state, single-binary deployment, TH1 scripting, and explicit provenance.

This is not a proposal to turn Fossil into Goose. The useful direction is to
adopt the parts that fit Fossil's model:

- declarative workflows instead of ad hoc prompt strings
- first-person integration testing of the agent itself
- diagnostics bundles for support and debugging
- explicit tool and capability registration
- safer repo-local guidance and policy injection
- scenario-based evals for regressions

## Observations From Goose

Relevant Goose patterns:

- `recipe.yaml` uses a declarative workflow format with metadata, instructions,
  activities, and allowed extensions.
- `goose-self-test.yaml` treats agent self-test as a first-person integration
  workflow with parameters, phases, success criteria, and reporting.
- diagnostics docs define a support bundle containing logs, session history,
  config, and system information.
- Goose exposes extensions and MCP tools as explicit, discoverable
  capabilities instead of burying them in prompts.
- Goose uses recipes and subrecipes as reusable operational units, not just one
  long system prompt.

Fossil already has partial building blocks:

- TH1 orchestration plan in [TH1_ORCHESTRATION.md](./TH1_ORCHESTRATION.md)
- persisted event model in [AGENT_EVENTS.md](./AGENT_EVENTS.md)
- repository-local AI schema in [SCHEMA.md](./SCHEMA.md)
- emerging pool processing and note graph primitives
- working CLI and `/agentui` surfaces in `src/agent.c`

The gap is mostly productization: Fossil has mechanisms, but not yet a
coherent workflow layer, diagnostics story, or reproducible eval harness.

## Principles

- Keep workflows repository-local and inspectable.
- Prefer TH1 and repository tables over opaque external state.
- Make capabilities explicit and auditable.
- Preserve offline/local-first operation.
- Bias toward deterministic tests and exportable diagnostics.
- Do not require MCP to get value; design a capability registry that can start
  with built-in Fossil and TH1 commands.

## Phase 1: Workflow Recipes

Objective:

- add a declarative layer for repeatable agent tasks

Why this is worth borrowing:

- Goose recipes package instructions, parameters, and tool access cleanly.
- Fossil currently has commands and planned TH1 orchestration, but no
  first-class reusable workflow object.

Proposal:

- introduce repository-managed "agent recipes" stored either:
  - as versioned files under a conventional path such as
    `repo:/agent/recipes/*.yaml`, or
  - in a new table such as `ai_recipe`
- support recipe fields:
  - `name`
  - `title`
  - `description`
  - `instructions`
  - `parameters`
  - `phases`
  - `allowed_capabilities`
  - `default_model_policy`
  - `success_criteria`
- add CLI and web entry points:
  - `fossil agent recipe list`
  - `fossil agent recipe show NAME`
  - `fossil agent recipe run NAME ?--param k=v?`
  - `/agent-recipes`
- compile recipes into TH1 orchestration calls rather than embedding YAML logic
  throughout C

Likely implementation touchpoints:

- `src/agent.c`
- `src/ai.c`
- `doc/ai/TH1_ORCHESTRATION.md`
- new tests in `tst/agent.test` and `tst/th1-agent.test`

Exit criteria:

- a repository can ship a reusable workflow for code review, release notes, or
  triage without hand-editing prompts every time

## Phase 2: First-Person Agent Self-Test

Objective:

- turn Fossil's current smoke-level AI testing into a structured agent
  integration suite

Why this is worth borrowing:

- Goose's self-test recipe is strong because it tests the agent using the same
  mechanisms real users exercise.

Proposal:

- extend `fossil ai selftest` and/or add `fossil agent selftest` with named
  phases:
  - schema
  - retrieval
  - note creation
  - pool processing
  - event persistence
  - TH1 orchestration
  - `/agentui` and API flows
- record pass/fail status and metrics in repository-local rows such as
  `ai_eval_run` and `ai_eval_case`
- support `--quick`, `--full`, and `--keep`
- produce a machine-readable summary JSON for CI

Likely implementation touchpoints:

- `src/ai.c`
- `src/agent.c`
- new schema tables in `doc/ai/SCHEMA.md`
- `tst/ai.test`
- `tst/agent.test`
- `tst/th1-agent.test`

Exit criteria:

- a single command exercises the end-to-end agent stack and leaves a concise
  artifact explaining what failed

## Phase 3: Diagnostics Bundle Export

Objective:

- make agent failures supportable without asking users to manually gather state

Why this is worth borrowing:

- Goose's diagnostics bundle is operationally valuable. Fossil already stores
  the core data needed for a repository-native version.

Proposal:

- add `fossil agent diagnostics` and optionally `/agent-diagnostics`
- export a ZIP or Fossil bundle-like archive containing:
  - agent session rows from `agentchat_session` and `agentchat`
  - effective config summary
  - recent AI tables relevant to the session (`ai_context`, `ai_retrieval`,
    `ai_retrieval_note`, `ai_review`, selected `ai_note` rows)
  - Fossil version, platform, and repository hash
  - redacted environment and command metadata
  - optional recent HTTP/agent log excerpts
- support:
  - `--sid`
  - `--since`
  - `--redact-secrets`
  - `--output`

Important constraint:

- diagnostics must default to safe redaction because Fossil repositories often
  include proprietary code and prompts.

Likely implementation touchpoints:

- `src/agent.c`
- `src/ai.c`
- possibly `src/zip.c` or existing archive helpers
- `doc/ai/PROVENANCE.md`
- `doc/ai/UI.md`

Exit criteria:

- a bug report can include one generated artifact instead of an error
  description plus hand-copied SQL output

## Phase 4: Capability Registry Instead Of Implicit Tooling

Objective:

- make agent-accessible actions discoverable, policy-bound, and testable

Why this is worth borrowing:

- Goose's extension model makes available tools explicit. Fossil currently has
  local commands and TH1 hooks, but capability exposure is still mostly
  implementation detail.

Proposal:

- add a capability registry layer for:
  - built-in Fossil operations
  - TH1-defined helper commands
  - optional external wrappers
- each capability should declare:
  - `name`
  - `kind`
  - `description`
  - `trust_level`
  - `requires_write`
  - `requires_network`
  - `requires_user_confirmation`
- surface this through:
  - `fossil agent capabilities`
  - `/agent-config` or `/agent-capabilities`
  - policy rows in `ai_policy`

This does not need full MCP support initially. The immediate win is replacing
implicit prompt text like "you may call X" with a structured allowlist that the
UI, CLI, tests, and TH1 orchestration all share.

Likely implementation touchpoints:

- `src/agent.c`
- `src/ai.c`
- `doc/ai/SCHEMA.md`
- `doc/ai/TH1_ORCHESTRATION.md`

Exit criteria:

- the active tool surface is inspectable before a session runs

## Phase 5: Repository Guidance And Policy Files

Objective:

- let repositories provide stable local instructions without hardcoding them in
  prompts or user config

Why this is worth borrowing:

- Goose uses reusable guidance and recipe files. Fossil needs a controlled
  equivalent for project-specific agent context.

Proposal:

- define one or both of:
  - a repository file such as `.fossil-agent.md`
  - a wiki-backed canonical guidance page
- guidance is injected into context as a typed source, not concatenated as
  anonymous text
- persist provenance in `ai_context` or `ai_note` with `source_type='doc'` or a
  new `policy`/`guidance` type
- support explicit precedence:
  - built-in safety policy
  - repo guidance
  - workflow recipe instructions
  - user prompt

Critical safeguard:

- guidance should be visible in diagnostics and in the UI so silent repo-level
  prompt manipulation is not hidden.

Likely implementation touchpoints:

- `src/agent.c`
- `src/ai.c`
- `doc/ai/PROVENANCE.md`
- `doc/ai/SCHEMA.md`

Exit criteria:

- repositories can encode durable local conventions without forking Fossil or
  asking users to remember setup steps

## Phase 6: Scenario Evals And Benchmarks

Objective:

- create regression detection for agent behavior, not just schema correctness

Why this is worth borrowing:

- Goose clearly invests in evals and replayable integration checks. Fossil
  needs a similar mechanism to prevent agent regressions as orchestration gets
  more ambitious.

Proposal:

- add repository-local eval definitions such as:
  - `tst/agent-evals/*.yaml`
- each eval defines:
  - starting repo fixture
  - prompt or recipe
  - allowed capabilities
  - expected event shapes
  - expected final text predicates
  - expected note/retrieval side effects
- support deterministic stub providers for CI
- emit comparable metrics:
  - duration
  - token counts
  - retrieval counts
  - failure reason

Likely implementation touchpoints:

- `src/agent.c`
- `src/ai.c`
- `tst/tester.tcl`
- new `tst/agent-evals/*.test` or YAML-driven harness

Exit criteria:

- Fossil can detect when a change breaks retrieval quality, event persistence,
  or orchestration semantics even if ordinary unit tests still pass

## Phase 7: Delegation And Multi-Step Execution

Objective:

- support structured multi-step workflows without pushing all logic into one
  giant prompt

Why this is worth borrowing:

- Goose recipes and delegation patterns show the value of explicit phases and
  limited sub-tasks.

Proposal:

- extend TH1 orchestration to support:
  - staged phases
  - bounded subtask execution
  - optional parallel retrieval/review work where deterministic
  - explicit intermediate checkpoints persisted as events
- avoid open-ended autonomous recursion
- tie every phase transition to a stored event row in `agentchat`

Important constraint:

- Fossil should prefer deterministic orchestration over autonomous agent trees.
  The goal is maintainable workflows, not maximum autonomy.

Likely implementation touchpoints:

- `src/agent.c`
- `doc/ai/AGENT_EVENTS.md`
- `doc/ai/TH1_ORCHESTRATION.md`

Exit criteria:

- complex tasks can be decomposed into explicit stages with reviewable progress

## Recommended Order

Recommended implementation sequence:

1. diagnostics bundle export
2. capability registry
3. recipe/workflow layer
4. structured self-test
5. repo guidance files
6. scenario evals
7. phased delegation support

Reasoning:

- diagnostics and capability discovery reduce debugging cost immediately
- recipes become safer once capabilities are explicit
- self-test and evals should validate the new workflow layer rather than
  precede it

## Things Not To Copy Directly

Goose patterns that do not map cleanly to Fossil:

- desktop-first UI assumptions
- heavy runtime dependence on MCP for baseline functionality
- broad dynamic extension installation during ordinary sessions
- cloud-style support flows that assume central services

Fossil should stay:

- repository-centric
- auditable
- local-first
- minimal in moving parts

## Near-Term Deliverables

If implemented incrementally, the best next three items are:

1. `fossil agent diagnostics --sid SID --output FILE`
2. `fossil agent capabilities`
3. `fossil agent recipe list|show|run`

That sequence gives Fossil a support story, a policy surface, and a reusable
workflow layer without waiting for a larger architecture rewrite.
