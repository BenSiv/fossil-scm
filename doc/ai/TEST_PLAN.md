# AI Feature Test Plan

## Schema
- Create tables on new repo
- No-op on existing repo with tables
- Create retrieval, review, and note-link tables without touching core Fossil schema

## Provenance
- Insert context rows
- Link to artifacts
- Persist prompts and reasoning traces as attributable data pool records

## Tiers
- Promote and sink notes
- Heat decay behavior
- Higher tiers receive higher base retrieval weight
- Retrieval count raises future retrieval likelihood

## Evaluation loop
- Detect non-atomic notes after retrieval
- Record co-retrieval links between notes
- Detect exact duplicates and preserve merge backlinks
- Retitle only when the current title is inaccurate
- Update processing-level metadata after review

## UI
- Pages render
- Queries return expected results
