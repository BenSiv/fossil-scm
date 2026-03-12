# AI-Optimized Software Management System on Fossil

## Status
Draft

## Summary
This document specifies an AI-optimized software management system built on Fossil SCM. The system automates versioning and documentation in the background, exposes a web-first UI, and manages a hierarchical knowledge base backed by Fossil artifacts and SQLite metadata. It is model- and provider-agnostic with minimal dependencies.

## Goals
- Provide a web-first interface for all user workflows.
- Automate micro-commits and documentation updates without explicit user action.
- Persist complete rationale, prompts, and decision trails.
- Maintain minimal dependencies: Fossil, SQLite, Tcl/TH1, and optional LLM adapters.
- Support rapid retrieval via vector embeddings and structured hierarchies.

## Non-Goals
- Replacing Fossil SCM core behavior or storage formats.
- Requiring external services for basic operation.
- Exposing raw backend internals to end users by default.
- Treating unrestricted raw chain-of-thought dumps as the primary long-term
  knowledge representation.

## Architecture Overview
- Fossil repository is the primary data store.
- A metadata layer extends Fossil’s SQLite database with AI-specific tables.
- Background automation creates micro-commits and manages embeddings.
- The web UI is the only required user interface.

## Data Model

### Artifact Types
- Code artifacts.
- Documentation artifacts.
- Prompt artifacts.
- Rationale artifacts.
- Configuration artifacts.

### Knowledge Hierarchy
1. Tier 0: Fleeting Notes
2. Tier 1: Interaction Threads
3. Tier 2: Draft Syntheses
4. Tier 3: Atomic Concepts

### Storage Mapping
- Tier 0: Technotes tagged `ai-raw`.
- Tier 1: Technotes or `ai_context` records.
- Tier 2: Wiki pages under namespace `draft/`.
- Tier 3: Wiki pages in the main namespace.

### Required Metadata Tables
- `ai_context` for prompt/rationale/model/token data.
- `ai_vector_map` linking embeddings to artifacts.
- `ai_skills` for skill manifests and versions.
- `ai_steering` for constitution and steering directives.

## Micro-Commit Engine

### Behavior
- File changes trigger background micro-commits.
- Each micro-commit links to a context snapshot.
- Commit frequency is time-sliced to avoid excessive churn.

### Required Metadata
- Prompt or triggering event.
- Rationale summary.
- Model and provider identifiers.
- Token usage and timestamp.
- Optional structured reasoning or decision-trail references when policy allows.

## Bubbling and Sinking

### Bubbling Triggers
- High retrieval frequency.
- Repeated references across recent commits.
- Strong cluster similarity.
- User-marked stabilization requests.

### Bubbling Flow
1. Identify a cluster from Tier 0 or Tier 1.
2. Create a Tier 2 draft synthesis.
3. Validate against Constitution and related Tier 3 docs.
4. Promote to Tier 3 with rationale.

### Sinking Triggers
- Low retrieval score over a decay window.
- No links from recent commits.
- Low semantic similarity to active objectives.

### Sinking Flow
- Reduce retrieval priority.
- Exclude from default context window.
- Keep searchable by explicit query.

## Context Bandwidth Management

### Context Window Composition
- Active files and diffs.
- Recent micro-commit rationales.
- Relevant Tier 3 atomic concepts.
- Active steering directives.
- User preferences and global prompts.

### Priority Bands
- High: current task artifacts.
- Medium: relevant atomic concepts.
- Low: raw notes and background signals.

### Gating Rules
- Hard token caps per band.
- Low priority truncated first.
- Overflow replaced by summaries.

### Audit
- Every included item logged with score and reason.
- UI exposes “why was this included” per item.

## Skill System

### Skill Manifest
- Name, version, and purpose.
- Trigger criteria.
- Allowed tools and constraints.
- System prompt fragment.
- Safety constraints.

### Lifecycle
- Skills are versioned in-repo.
- Updates require explicit approval.
- Deprecated skills remain for audit.

### Safety Gates
- Each skill declares forbidden actions.
- Execution requires alignment check against Constitution.
- High-risk skills require user confirmation.

## Constitution and Steering

### Constitution Contents
- Project goals and scope.
- Coding and documentation standards.
- Security and privacy requirements.
- Forbidden behaviors.
- Rules for how reasoning artifacts are summarized, stored, and retrieved.

### Steering Workflow
- User steering updates a steering document.
- Steering is prioritized in the context window.
- New commits reference steering changes.

### Drift Detection
- Periodic scans of recent commits.
- Compare rationale to Constitution.
- Flag deviations into a review queue.

## Vectorization and Retrieval

### Embedding Sources
- Code diffs and check-ins.
- Wiki and technotes.
- Prompts and configurations.
- Chat transcripts.

### Retrieval Scoring
- Similarity score.
- Recency weight.
- Heat value.
- Task relevance signal.

### Decay
- Exponential decay on heat.
- Retrieval or reference resets heat.

## Web UI

### Primary Interfaces
- Chat interface.
- Wiki interface.
- Change log with micro-commit stream.
- Task management view.
- Analytics dashboard.

### User Controls
- Promote and demote concepts.
- Pin or freeze artifacts.
- Override steering.
- Inspect context selection.

## Background Automation

### Automation Events
- File save triggers micro-commit.
- Commit triggers embedding and metadata capture.
- Daily synthesis jobs for Tier 2.
- Weekly consolidation for Tier 3.

### Resilience
- Job retries on failure.
- Text-only fallback when embeddings unavailable.
- User-visible job status and history.

## Analytics and Metrics

### Knowledge Metrics
- Tier density ratios.
- Promotion velocity.
- Concept coverage.

### Alignment Metrics
- Constitution adherence rate.
- Drift incident count.
- User intervention rate.

### Productivity Metrics
- Commit cadence.
- Task throughput.
- Change volatility.

## Operational Constraints

### Dependencies
- Fossil, SQLite, Tcl/TH1 as baseline.
- Optional embedding provider adapters.

### Security
- Local storage by default.
- Configurable retention policies.
- Redaction for sensitive content.

## Acceptance Criteria

### Core UX
1. All primary workflows are accessible via the web UI.
2. User can view micro-commit stream and rationale.
3. User can inspect context selection and its rationale.

### Data and Knowledge
1. All four tiers are represented and discoverable.
2. Vector map references resolve to Fossil artifacts.
3. Promotions and demotions are recorded with rationale.

### Automation
1. Micro-commit creation occurs within configured window after changes.
2. Embedding pipeline recovers from temporary failures.
3. The system operates without embeddings when disabled.

### Alignment
1. Steering updates affect subsequent commits.
2. Drift detection produces a review queue.
3. High-risk actions require explicit approval.

## Open Questions
- What default thresholds should be used for bubbling and sinking?
- What is the default embedding provider and vector dimension?
- What are the initial token caps for context bands?
- Which UI controls are mandatory for v1?
