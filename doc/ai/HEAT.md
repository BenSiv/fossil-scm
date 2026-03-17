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

- Low heat pushes notes to lower priority retrieval without removing them.

## Identified Risks & Pitfalls

Relying purely on an adaptive "heat" score can introduce problems in continuous engineering environments:

1. **The "Echo Chamber" Problem (Starvation):** If information only bubbles up because it is frequently cited, it creates a feedback loop where "hot" but outdated code patterns dominate, suppressing "cold" but more modern, efficient, or secure solutions. You trade accuracy for recency.
2. **"Heat" is a Poor Proxy for "Truth":** The most active or debated information in a fast-moving codebase is often the most broken. High activity does not mean high authority. Without mechanisms to override heat, the system might bubble up problems instead of proven solutions.
3. **The "Cold Start" Paradox:** Brand-new, high-value documentation begins with zero metrics. Left alone, the adaptive system may ignore a critical new architecture document in favor of an older, "hotter" legacy one.
4. **Semantic Fragmentation:** Relying on inferred importance from isolated retrieval metrics risks losing the explicit, human-curated relationships (parent/child, contradictions) that provide real meaning. 
5. **SQLite Computational Bottleneck:** Calculating complex heat scores (combining frequency, recency, density, and validation metrics) dynamically across thousands of artifacts for every single agent turn can become computationally expensive, introducing unacceptable latency.

## Proposed Patches / Refinements

To counteract these pitfalls, the heat model must incorporate additional mechanisms:

- **The "Thermostat" (Decay Rate):** Different data types must cool at different speeds. Fundamental protocols (like security standards or core patterns) should remain "warm" indefinitely, whereas ephemeral daily stand-up notes should freeze rapidly.
- **The "Icebreaker" (Exploration):** Introduce an epsilon-greedy algorithm equivalent. Occasionally force the agent to explore "cold" but semantically relevant notes to discover better solutions and escape local maxima.
- **Strong Typing (Artifact Weighting):** Leverage Fossil's built-in artifact capabilities to apply static weight. For example, a check-in comment or a canonical Wiki page can be weighted to bubble faster and more reliably than a chat message.
