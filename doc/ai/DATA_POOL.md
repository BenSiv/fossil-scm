# Data Pool Strategy

Purpose: define how all AI-relevant material enters a shared note pool, how
metadata drives retrieval, and how retrieved notes are refined over time.

## Ingestion policy

Any AI-relevant material should be eligible for capture in the data pool,
including:

- prompts
- chain-of-thought or internal reasoning traces
- documentation
- wiki pages
- tickets
- chat transcripts
- diffs and micro-commits
- derived summaries and curated notes

Each captured item enters the pool as a note or note-linked artifact with
metadata attached at ingest time.

## Required metadata

Every pooled note should carry enough metadata to support provenance,
retrieval, and later curation:

- stable note ID
- title
- source type
- source reference or artifact ID
- created and updated timestamps in UTC
- tier or curation level
- processing level category
- retrieval count
- last retrieved timestamp
- duplicate-of or merged-into link when applicable
- related note links
- content hash for exact duplicate detection

## Note hierarchy

The note hierarchy expresses how curated a note is. More curated notes should
be more likely to be retrieved as model context.

- Tier 0: raw captures such as prompts, chain-of-thought, diffs, and imported
  source material
- Tier 1: lightly processed working notes and grouped retrieval sessions
- Tier 2: curated draft notes with clearer boundaries, titles, and links
- Tier 3: atomic notes representing one durable subject

Higher tiers get a stronger base retrieval weight. Lower tiers remain
available for provenance, recovery, and re-processing.

## Retrieval strategy

Retrieval is metadata-first and tier-aware.

- Use metadata filters and source links before broad semantic expansion.
- Prefer higher-tier notes when they cover the query.
- Fall back to lower-tier notes when higher-tier coverage is weak or missing.
- Increase a note's future retrieval likelihood each time it is retrieved.
- Increase relationship strength between notes that are repeatedly retrieved
  together.

This creates a reinforcement loop: curation raises retrieval quality, and
successful retrieval raises future retrieval probability.

## Post-retrieval evaluation loop

Any retrieval event should trigger a processing loop over the retrieved notes.

### 1. Atomicity

Check whether the note covers one subject. Split or flag notes that mix
multiple unrelated subjects.

### 2. Connectivity

Record which notes were retrieved together and strengthen links between notes
that repeatedly co-occur.

### 3. Duplication and merge

Detect exact duplicates first, then near-duplicates. Merge when the retrieved
notes contain the same information and preserve backlinks to the absorbed
notes.

### 4. Title accuracy

Check whether the title still accurately names the note's subject. Retitle only
when accuracy requires it.

### 5. Metadata and processing level

Update metadata to reflect the latest processing state, including categories
that describe how processed or curated the note is.

## Expected outcomes

- the pool accepts all relevant material instead of discarding raw inputs
- metadata remains rich enough to reconstruct origin and processing history
- retrieval quality improves as notes are curated and repeatedly reused
- the evaluation loop steadily pushes useful notes toward atomic,
  better-connected, de-duplicated forms
