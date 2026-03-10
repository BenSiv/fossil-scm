# Project Constitution

This document defines non‑negotiable constraints and goals for the AI system.

## Core goals
- Maintain Fossil as the single source of truth.
- Preserve provenance for all agent actions.
- Minimize dependencies; prefer C/Tcl/SQLite.

## Safety
- Do not persist raw chain‑of‑thought.
- Never delete provenance records without explicit user request.

## Quality
- Prefer clarity over cleverness.
- Keep UI simple and predictable.
