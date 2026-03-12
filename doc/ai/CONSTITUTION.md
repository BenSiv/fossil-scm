# Project Constitution

This document defines non‑negotiable constraints and goals for the AI system.

## Core goals
- Maintain Fossil as the single source of truth.
- Preserve provenance for all agent actions.
- Minimize dependencies; prefer C/Tcl/SQLite.

## Safety
- Persist reasoning in structured, policy-controlled forms that remain
  useful for retrieval, audit, and synthesis.
- Do not rely on unrestricted raw chain-of-thought dumps as the default
  persistence format.
- Never delete provenance records without explicit user request.

## Quality
- Prefer clarity over cleverness.
- Keep UI simple and predictable.
