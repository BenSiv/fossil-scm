# Open Knowledge Commons Schema

Scope: define the SQLite extension tables used by the knowledge-layer features
in this Fossil fork.

## Core Tables

- `ai_note`
  - Purpose: canonical pooled note record for raw, working, and durable-linked
    knowledge.
  - Key fields:
    - `nid` INTEGER PRIMARY KEY
    - `tier` INTEGER
    - `title` TEXT
    - `body` TEXT
    - `source_type` TEXT
    - `source_id` INTEGER
    - `source_ref` TEXT
    - `process_level` TEXT
    - `metadata` TEXT
    - `artifact_kind` TEXT
    - `artifact_ref` TEXT
    - `artifact_rid` INTEGER
    - `artifact_path` TEXT
    - `artifact_status` TEXT
    - `heat` REAL DEFAULT 1.0
    - `retrieval_count` INTEGER DEFAULT 0
    - `last_retrieved_at` TEXT
    - `content_hash` TEXT
    - `duplicate_of` INTEGER REFERENCES ai_note
    - `merged_into` INTEGER REFERENCES ai_note
    - `created_at` TEXT
    - `updated_at` TEXT

- `ai_note_link`
  - Purpose: explicit graph edges and lineage between notes.
  - Key fields:
    - `from_nid` INTEGER REFERENCES ai_note
    - `to_nid` INTEGER REFERENCES ai_note
    - `link_type` TEXT
    - `weight` REAL DEFAULT 1.0
    - `updated_at` TEXT

- `ai_retrieval`
  - Purpose: audit retrieval events.
  - Key fields:
    - `qid` INTEGER PRIMARY KEY
    - `query_text` TEXT
    - `created_at` TEXT

- `ai_retrieval_note`
  - Purpose: note-level results for one retrieval event.
  - Key fields:
    - `qid` INTEGER REFERENCES ai_retrieval
    - `nid` INTEGER REFERENCES ai_note
    - `rank` INTEGER
    - `score` REAL
    - `tier_weight` REAL
    - `reinforcement_delta` REAL

- `ai_review`
  - Purpose: store post-retrieval review and promotion decisions.
  - Key fields:
    - `review_id` INTEGER PRIMARY KEY
    - `qid` INTEGER REFERENCES ai_retrieval
    - `nid` INTEGER REFERENCES ai_note
    - `atomicity_status` TEXT
    - `connectivity_status` TEXT
    - `duplication_status` TEXT
    - `title_status` TEXT
    - `promotion_status` TEXT
    - `action_summary` TEXT
    - `created_at` TEXT

- `ai_vector`
  - Purpose: optional embeddings storage for semantic retrieval.
  - Key fields:
    - `vid` INTEGER PRIMARY KEY
    - `source_type` TEXT
    - `source_id` INTEGER
    - `dim` INTEGER
    - `vector` BLOB

## Schema Principles

- provenance fields describe where a note came from
- artifact fields describe durable publication targets
- retrieval tables are the audit trail for reuse
- review tables explain promotion and cleanup decisions
- durable human-readable text should eventually live in Fossil artifacts rather
  than SQL-only blobs

## Table Creation Strategy

- create missing tables on first knowledge-feature use or during schema upgrade
- keep the extension isolated from core Fossil tables
- permit operation without optional embedding support

## Compatibility

- no changes to existing core Fossil tables are required
- all knowledge-layer tables remain optional for repositories not using these
  features
