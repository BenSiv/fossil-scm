# Bubbling Context: A Self-Adaptive Knowledge System for AI Agents

AI agents do not just need more context. They need the right context to rise at the right time.

Most current systems still treat knowledge as static documentation, scattered notes, flat retrieval, or fixed instruction bundles. That is not enough for long-running work across evolving codebases, shifting priorities, and incomplete human intent.

I think a better model is a knowledge pool.

In a knowledge pool, notes, decisions, observations, and documentation all enter a shared system where they are continuously re-evaluated through use.

Hot information bubbles up.

These are the notes, concepts, and decisions that are repeatedly retrieved, cited, linked, reused, or validated in active work. Because they keep proving useful, they rise closer to the surface and are more likely to become part of the working context for new tasks.

Cold information sinks.

It does not disappear, but it moves deeper in the pool so it remains available without crowding the active context.

This creates a better system for AI-assisted work:

- recent and task-relevant knowledge gets prioritized
- stable, high-value concepts become durable reference points
- transient notes remain available without polluting the hot path
- importance emerges from actual use
- context becomes dynamic instead of manually assembled

This is also why I think this is more useful than traditional note systems alone.

Zettelkasten is valuable for atomic notes, linking, and long-term accumulation of ideas. But it depends heavily on intentional human curation. Someone has to decide what to rewrite, what to connect, what to elevate, and what to revisit.

In a fast-moving engineering environment, that makes knowledge management a separate maintenance task.

A knowledge pool works differently. Importance is inferred from retrieval, reuse, and successful task execution. The system keeps reorganizing knowledge as part of normal work.

The same problem exists with static context systems such as `AGENTS.md`, `SKILLS`, fixed system prompts, and other manually maintained instruction bundles. They are useful as steering layers, but they are still static. They keep consuming tokens even when much of that context is no longer relevant.

Static context tells the agent what kind of system it is operating in.

Adaptive context helps the agent decide what matters right now.

I also think Fossil-SCM is an unusually strong base platform for building this kind of system. It already combines a structured SQLite-backed repository, source control, wiki, tickets, chat, artifact history, and a web UI in one integrated system. That makes it a much better substrate for AI-native knowledge management than bolting memory onto a loose collection of external tools.

The real opportunity is not just giving agents bigger context windows. It is building systems where knowledge reorganizes itself through retrieval and use.

I’m actively working on this in my Fossil-SCM fork and would love contributions from people interested in AI agents, retrieval, knowledge systems, and developer tools.

Repository: https://github.com/BenSiv/fossil-scm

If you are working on AI agents, retrieval systems, developer tools, or knowledge management for software development, I would be interested in comparing notes.

## LinkedIn Version

**Bubbling Context: A Self-Adaptive Knowledge System for AI Agents**

**AI agents do not just need more context. They need the right context to rise at the right time.**

Most current systems still treat knowledge as static documentation, scattered notes, flat retrieval, or fixed instruction bundles.

I think a better model is a **knowledge pool**.

In a knowledge pool, notes, decisions, observations, and documentation are continuously re-evaluated through use.

**Hot information bubbles up.**
The notes and concepts that are repeatedly retrieved, cited, reused, or validated rise closer to the surface and become easier to include in active context.

**Cold information sinks.**
It does not disappear, but it moves deeper so it stays available without crowding the hot path.

This gives us:

- retrieval prioritization based on actual use
- automatic construction of an importance hierarchy
- dynamic context assembly for each task
- durable memory without turning everything into prompt clutter

I think this is much more useful than systems where knowledge management is a separate deliberate task.

That includes both traditional note systems and static context files like **AGENTS.md** and **SKILLS**. Those can be useful steering layers, but they are still static and they keep consuming tokens even when much of the content is no longer relevant.

**Static context tells the agent what kind of system it is operating in. Adaptive context helps the agent decide what matters right now.**

I also think **Fossil-SCM** is one of the best base platforms for building this kind of system. It already combines a structured SQLite-backed repository, source control, wiki, tickets, chat, artifact history, and a web UI in one integrated environment.

**The real opportunity is not just giving agents bigger context windows. It is building systems where knowledge reorganizes itself through retrieval and use.**

I’m actively working on this in my Fossil-SCM fork and would love contributions.

Repository: https://github.com/BenSiv/fossil-scm

If you are working on AI agents, retrieval systems, developer tools, or knowledge management for software development, I would be interested in comparing notes.

## LinkedIn Formatting Notes

- Bold the title.
- Bold the short thesis line near the top.
- Bold `Hot information bubbles up.`
- Bold `Cold information sinks.`
- Bold `AGENTS.md`, `SKILLS`, and the static vs adaptive context line.
- Bold `Fossil-SCM`.
- Bold the closing thesis line.
