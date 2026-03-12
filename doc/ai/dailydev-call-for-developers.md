# Daily.dev Draft: Call for Developers

## Title options

1. Building an AI-native dev environment on top of Fossil SCM
2. Looking for developers to help build an AI-first software workbench
3. Can a source repository become an AI agent's long-term memory?

## Final text-post version

I’m looking for developers who want to help build an AI-native software development environment on top of Fossil SCM.

The idea is to stop treating the repository as just a place to store code. Instead, the repository becomes the working memory of the project: source code, prompts, rationale, notes, evolving concepts, retrieval metadata, and a durable audit trail of how decisions were made.

Why Fossil? Because Fossil already gives us something most dev tools don’t: a structured SQLite repository with version control, wiki, tickets, chat, a built-in web UI, and artifact history in one system. That makes it a strong base for building a serious environment for AI-agent-enabled software development.

This fork is exploring:

- repository-backed provenance for agent actions
- automatic micro-commits
- a tiered knowledge model for raw notes, working context, drafts, and stable concepts
- semantic retrieval / RAG over repository knowledge
- a web-first interface for chat, review, knowledge inspection, and task flow
- a self-maintaining knowledge pool where useful ideas bubble up and stale ones sink

One of the main goals is to build project memory that does not decay into prompt logs nobody can use.

The knowledge model is based on a pool strategy:

- ideas bubble up when they are retrieved often, reused, linked, or validated
- ideas sink when they cool down or stop helping active work
- nothing important is lost, but not everything stays in the hot path

So this is not just "AI inside the editor." It is an attempt to make the repository itself into an integrated environment for code, context, reasoning, and long-term knowledge management.

There is already early implementation work in this repo, including AI-specific SQLite tables for context, notes, vectors, and policy, commit-linked provenance capture, local agent chat in the Fossil web UI, note storage and semantic indexing/search plumbing, and design docs for context assembly, steering, constitution, tiers, metrics, and UI surfaces.

I’m especially interested in people who care about Fossil internals and C development, SQLite as an application platform, retrieval and vector search, local-first or low-dependency developer tools, AI workflows with strong provenance and inspectability, and knowledge systems that improve over time instead of growing stale.

If current AI coding tools feel stateless, opaque, or too dependent on external platforms, this project may be interesting to you.

If there’s interest, I can also share a follow-up post with the architecture, the current implementation status, and a concrete contributor roadmap.

Repository: https://github.com/BenSiv/fossil-scm

## Shorter version

I’m looking for developers to help build an AI-native dev environment on top of Fossil SCM.

The thesis is that the repository should hold more than source code. It should also hold context, rationale, notes, retrieval metadata, and durable project knowledge that an AI agent can use and maintain over time.

Fossil is a strong base because the repo is already a structured SQLite database with version control, wiki, tickets, chat, and a built-in web UI.

This fork explores provenance-aware agent actions, automatic micro-commits, tiered knowledge capture, semantic retrieval / RAG, and a pool-style memory model where useful concepts bubble up and stale ones sink.

There is already early work in the repo for AI schema, provenance, note storage, semantic indexing, and a local agent UI.

Repository: https://github.com/BenSiv/fossil-scm

If you work in C, SQLite, retrieval, AI tooling, or developer infrastructure, I’d like to connect.

## Posting notes

- Post this as a text post, not a link post.
- Leave the URL field empty if you want the body text to be primary.
- Put the repository URL inside the body, as shown above.
