# Open Knowledge Commons On Fossil

## Status

Draft

## Summary

This document describes an Open Knowledge Commons built on Fossil SCM. The
system extends Fossil with a local-first knowledge layer for indexed notes,
durable knowledge artifacts, provenance-aware retrieval, and self-hosted
deployment by small teams and public-interest organizations.

## Goals

- provide a browseable knowledge layer inside Fossil
- preserve provenance from working notes to durable artifacts
- promote useful knowledge into reviewable wiki or file-backed targets
- keep deployment lightweight and self-hosted
- support retrieval without turning the project into a closed hosted platform

## Non-Goals

- positioning the project as a generic AgentOps framework
- centering the roadmap on provider-routing or orchestration experiments
- making autonomous micro-commits the headline workflow
- requiring external hosted services for baseline operation

## Architecture Overview

- Fossil remains the primary collaboration and artifact system
- SQLite stores note metadata, retrieval history, and review records
- Fossil artifacts store durable promoted knowledge
- web UI surfaces browsing, provenance, review, and promotion workflows

## Core Capabilities

### Knowledge Browser

- browse indexed notes and knowledge objects
- filter by tier, source type, processing level, and artifact status
- inspect retrieval history and review queues

### Durable Artifact Layer

- promote draft notes into wiki or file-backed artifacts
- track draft, materialized, stale, and superseded states
- preserve linkage between pooled notes and durable artifacts

### Provenance-Aware Retrieval

- record retrieval events and note-level results
- expose source linkage and note lineage
- support review of promotion decisions and cleanup actions

### Deployability

- support local-first, self-hosted operation
- document setup, upgrade, and maintenance expectations
- keep the system usable by small teams without a large service stack

## Data Model

### Knowledge Tiers

1. Tier 0: raw captures and imports
2. Tier 1: working notes and grouped summaries
3. Tier 2: draft syntheses
4. Tier 3: durable concepts and canonical references

### Storage Mapping

- Tier 0 and Tier 1 commonly start in `ai_note`
- Tier 2 and Tier 3 should increasingly materialize as Fossil-native artifacts
- SQLite remains the metadata, retrieval, and audit layer

## Trust Model

- source metadata is preserved on ingest
- retrieval and review actions remain queryable
- durable artifacts link back to their source notes or artifacts
- human review remains the gate for durable publication

## Delivery Shape

The implementation roadmap is defined in `doc/ai/IMPLEMENTATION_PLAN.md` and
its companion documents under `doc/ai/`.
