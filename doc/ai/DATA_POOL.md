# Data Pool Strategy

Purpose: define how knowledge enters the shared pool, how metadata supports
retrieval, and how useful material advances toward durable artifacts.

## Ingestion Policy

Any project-relevant knowledge should be eligible for capture, including:

- notes
- documentation
- wiki pages
- technotes
- tickets
- linked source references
- draft summaries
- curated knowledge artifacts

The pool exists to reduce fragmentation between discussion, documentation, and
retrieval. Captured items should remain attributable to their source.

## Required Metadata

Every pooled note should carry enough metadata to support provenance,
retrieval, and promotion:

- stable note ID
- title
- source type
- source reference or artifact ID
- created and updated timestamps in UTC
- tier or curation level
- processing level
- retrieval count
- last retrieved timestamp
- duplicate-of or merged-into link when applicable
- related note links
- content hash for exact duplicate detection

## Note Hierarchy

- Tier 0: raw captures and imports
- Tier 1: working notes and grouped summaries
- Tier 2: draft syntheses ready for promotion review
- Tier 3: durable concepts and canonical references

Higher tiers should receive stronger default retrieval weight. Lower tiers
remain available for provenance, recovery, and backfill.

## Retrieval Strategy

Retrieval is metadata-first and tier-aware.

- prefer higher-tier notes when they cover the request
- fall back to lower tiers when higher-tier coverage is weak or missing
- preserve source links for every retrieved note
- increase future retrieval likelihood when a note is repeatedly reused
- strengthen links between notes that are repeatedly retrieved together

This creates a reinforcement loop in which curation improves retrieval quality
and demonstrated reuse helps identify promotion candidates.

## Post-Retrieval Review Loop

Any retrieval event should support a review loop over the retrieved notes.

### 1. Atomicity

Check whether the note covers one durable subject. Split or flag notes that mix
unrelated topics.

### 2. Connectivity

Record which notes were retrieved together and strengthen durable links between
repeated co-occurrences.

### 3. Duplication And Merge

Detect exact duplicates first, then near-duplicates. Preserve lineage when
merging or superseding notes.

### 4. Title Accuracy

Retitle only when the current title is misleading or no longer names the
subject well.

### 5. Promotion Readiness

Assess whether the note is ready to remain in the pool, become a draft artifact,
or be promoted into a durable artifact.

## Expected Outcomes

- project knowledge remains attributable instead of scattered
- retrieval quality improves through curation and repeated reuse
- useful notes move toward durable publication targets
- provenance stays available even when material becomes more polished
