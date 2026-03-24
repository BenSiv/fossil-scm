# Open Knowledge Commons Test Plan

## Schema

- create the knowledge-layer tables on a new repository
- no-op safely on repositories where the tables already exist
- verify durable artifact fields and review tables are initialized correctly

## Ingestion And Pooling

- insert notes across all tiers
- preserve source metadata on ingest
- detect exact duplicates via content hash
- preserve note lineage when merge or supersession metadata is applied

## Retrieval

- return expected notes for text and semantic lookup paths
- prefer higher-tier notes when they satisfy the request
- increment retrieval counts and update last-retrieved timestamps
- record note-level retrieval results for audit and review

## Promotion And Durable Artifacts

- link notes to wiki-backed or file-backed artifact targets
- transition artifact state through `draft`, `materialized`, `stale`, and
  `superseded`
- verify provenance survives promotion into a durable artifact

## Provenance And Review

- reconstruct note origin from source fields
- surface retrieval history for a note
- record duplicate, merge, and promotion decisions in review tables
- verify stale and superseded artifacts remain traceable

## UI

- knowledge pages render on first use
- filters for tier, source type, processing level, and artifact state work
- browse views link correctly to source and durable artifacts
- review queues and retrieval history pages return stable results

## Automation

- `make test` runs the Tcl harness in `tst/tester.tcl`
- repository-local tests remain hermetic and do not require network access
- web coverage can run through temporary local repositories and fake backends
