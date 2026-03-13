# Heat Model

Heat is the retrieval reinforcement signal used to prioritize notes in the data
pool.

## Inputs
- Access frequency
- Recency
- Usage in commits
- Similarity to current tasks
- Note tier or curation level
- Co-retrieval frequency with other notes

## Decay
- Heat decays over time if not accessed.
- Minimum floor prevents loss of provenance.

## Outputs
- Repeated retrieval increases future retrieval likelihood.
- High heat drives bubbling toward more prominent retrieval and later promotion.
- Low heat pushes notes to lower priority retrieval without removing them.
