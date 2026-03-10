# Heat Model

Heat is a decay-aware weight used to prioritize retrieval and promotion.

## Inputs
- Access frequency
- Recency
- Usage in commits
- Similarity to current tasks

## Decay
- Heat decays over time if not accessed.
- Minimum floor prevents loss of provenance.

## Outputs
- High heat drives bubbling to higher tiers.
- Low heat pushes notes to lower priority retrieval.
