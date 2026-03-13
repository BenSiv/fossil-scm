# Project Constitution

This document defines non-negotiable constraints and goals for the AI system.

## Core goals
- Maintain Fossil as the single source of truth.
- Preserve provenance for all agent actions.
- Preserve the full data pool, from raw captures to atomic notes.
- Minimize dependencies; prefer C/Tcl/SQLite.

## Safety
- Do not silently drop AI-relevant material from the data pool.
- Persist reasoning traces only as explicit pool artifacts with provenance and metadata.
- Never delete provenance records without explicit user request.

## Quality
- Prefer clarity over cleverness.
- Keep UI simple and predictable.
