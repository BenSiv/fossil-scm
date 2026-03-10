# AI Schema (Repository Extension)

Scope: define additional SQLite tables in the Fossil repository database using an `ai_` prefix.

## Tables

- `ai_context`
  - Purpose: per-action provenance for agent operations.
  - Fields:
    - `cid` INTEGER PRIMARY KEY
    - `rid` INTEGER REFERENCES blob   -- artifact linkage when applicable
    - `prompt` TEXT                  -- user prompt or system request
    - `rationale` TEXT               -- concise justification (not full chain-of-thought)
    - `model_id` TEXT                -- provider/model identifier
    - `token_in` INTEGER
    - `token_out` INTEGER
    - `created_at` TEXT              -- UTC timestamp
    - `tags` TEXT                    -- comma-separated or JSON

- `ai_note`
  - Purpose: raw/working notes tied to tiers.
  - Fields:
    - `nid` INTEGER PRIMARY KEY
    - `tier` INTEGER                 -- 0..3
    - `title` TEXT
    - `body` TEXT
    - `source_type` TEXT             -- prompt|diff|wiki|ticket|chat
    - `source_id` INTEGER            -- references ai_context or artifact rid
    - `heat` REAL DEFAULT 1.0
    - `created_at` TEXT
    - `updated_at` TEXT

- `ai_vector`
  - Purpose: embeddings storage (optional). Implemented via FTS5+aux table or a VSS virtual table if enabled.
  - Fields:
    - `vid` INTEGER PRIMARY KEY
    - `source_type` TEXT
    - `source_id` INTEGER
    - `dim` INTEGER
    - `vector` BLOB                  -- opaque; exact format defined by embedding backend

- `ai_policy`
  - Purpose: model routing and safety policies.
  - Fields:
    - `key` TEXT PRIMARY KEY
    - `value` TEXT
    - `updated_at` TEXT

## Table creation strategy

- Preferred: create tables during repository schema upgrade if missing.
- Fallback: lazy-create tables when the first AI feature is used.

## Compatibility

- No changes to existing Fossil tables.
- All AI tables are optional; system functions without AI features.
