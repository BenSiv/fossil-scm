# AI Schema (Repository Extension)

Scope: define additional SQLite tables in the Fossil repository database using an `ai_` prefix.

## Tables

- `ai_context`
  - Purpose: per-action provenance for agent operations.
  - Fields:
    - `cid` INTEGER PRIMARY KEY
    - `rid` INTEGER REFERENCES blob   -- artifact linkage when applicable
    - `prompt` TEXT                  -- user prompt or system request
    - `rationale` TEXT               -- concise summary or reasoning reference
    - `reasoning_note_id` INTEGER REFERENCES ai_note
    - `model_id` TEXT                -- provider/model identifier
    - `token_in` INTEGER
    - `token_out` INTEGER
    - `created_at` TEXT              -- UTC timestamp
    - `tags` TEXT                    -- comma-separated or JSON

- `ai_note`
  - Purpose: canonical data pool record for raw, working, and curated notes.
  - Fields:
    - `nid` INTEGER PRIMARY KEY
    - `tier` INTEGER                 -- 0..3
    - `title` TEXT
    - `body` TEXT
    - `source_type` TEXT             -- prompt|reasoning|diff|wiki|ticket|chat|doc|summary
    - `source_id` INTEGER            -- references ai_context or artifact rid
    - `source_ref` TEXT              -- durable reference when source_id is insufficient
    - `process_level` TEXT           -- raw|grouped|curated|atomic
    - `metadata` TEXT                -- JSON metadata payload
    - `heat` REAL DEFAULT 1.0
    - `retrieval_count` INTEGER DEFAULT 0
    - `last_retrieved_at` TEXT
    - `content_hash` TEXT
    - `duplicate_of` INTEGER REFERENCES ai_note
    - `merged_into` INTEGER REFERENCES ai_note
    - `created_at` TEXT
    - `updated_at` TEXT

- `ai_note_link`
  - Purpose: explicit graph edges between notes.
  - Fields:
    - `from_nid` INTEGER REFERENCES ai_note
    - `to_nid` INTEGER REFERENCES ai_note
    - `link_type` TEXT               -- related|duplicate|parent|child|merged|co_retrieved
    - `weight` REAL DEFAULT 1.0
    - `updated_at` TEXT

- `ai_retrieval`
  - Purpose: audit retrieval events and trigger the evaluation loop.
  - Fields:
    - `qid` INTEGER PRIMARY KEY
    - `context_id` INTEGER REFERENCES ai_context
    - `query_text` TEXT
    - `created_at` TEXT

- `ai_retrieval_note`
  - Purpose: note-level retrieval results for one retrieval event.
  - Fields:
    - `qid` INTEGER REFERENCES ai_retrieval
    - `nid` INTEGER REFERENCES ai_note
    - `rank` INTEGER
    - `score` REAL
    - `tier_weight` REAL
    - `reinforcement_delta` REAL

- `ai_review`
  - Purpose: store the post-retrieval evaluation loop decisions.
  - Fields:
    - `review_id` INTEGER PRIMARY KEY
    - `qid` INTEGER REFERENCES ai_retrieval
    - `nid` INTEGER REFERENCES ai_note
    - `atomicity_status` TEXT
    - `connectivity_status` TEXT
    - `duplication_status` TEXT
    - `title_status` TEXT
    - `metadata_status` TEXT
    - `action_summary` TEXT
    - `created_at` TEXT

- `ai_chat_eval`
  - Purpose: store lightweight answer-evaluation rows for persisted chat
    outcomes.
  - Fields:
    - `eval_id` INTEGER PRIMARY KEY
    - `sid` INTEGER REFERENCES agentchat_session
    - `acid` INTEGER REFERENCES agentchat
    - `provider` TEXT
    - `model` TEXT
    - `reply_kind` TEXT             -- final|error|reasoning-visible|empty
    - `quality_status` TEXT         -- ok|error|review|empty
    - `reasoning_status` TEXT       -- none|visible
    - `user_feedback` TEXT          -- useful|not-useful
    - `feedback_at` TEXT
    - `action_summary` TEXT
    - `created_at` TEXT

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
