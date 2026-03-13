# Hacker News Draft: Call for Developers

## Title options

1. Show HN: Building an AI-native software development environment on top of Fossil SCM
2. Ask HN: Developers interested in an AI-native dev environment built on Fossil + SQLite?
3. Call for developers: Fossil SCM as an integrated environment for AI-agent software development

## Draft post

I am looking for developers who want to help build an integrated environment for AI-agent-enabled software development on top of Fossil SCM.

The core idea is simple: treat the repository itself as the durable working memory for software development, not just as a place to store source code. Fossil is a strong base for this because the repo is already a structured SQLite database with built-in versioning, wiki, chat, tickets, web UI, and artifact history.

This fork is exploring how to extend Fossil into an AI-native development environment with:

- repository-backed provenance for agent actions
- automatic micro-commits with prompt/rationale metadata
- a tiered knowledge system that keeps raw notes, working context, draft syntheses, and durable atomic concepts
- semantic retrieval over repository knowledge using embeddings and vector search
- a web-first interface for chat, task flow, knowledge inspection, and change review

The knowledge-management model is based on a pool strategy: ideas bubble up when they are repeatedly retrieved, referenced, or validated, and sink when they cool down. The goal is not just better RAG, but a self-maintaining project memory that stays useful over time instead of turning into an unstructured log of prompts and chats.

The current repo already contains early pieces of this direction:

- AI-specific SQLite schema for context, notes, vectors, and policy
- provenance capture tied to commits
- local agent chat integration in the Fossil web UI
- note storage plus semantic indexing/search plumbing
- docs for context assembly, tiers, steering, constitution, metrics, and UI surfaces

The design principles are:

- Fossil remains the source of truth
- SQLite is the substrate
- minimal dependencies
- model/provider agnostic interfaces
- structured reasoning and provenance captured as retrievable repository knowledge
- strong provenance and inspectability

What I need help with:

- Fossil/C development
- SQLite schema and query design
- vector search and retrieval quality, including sqlite-vss integration or equivalent approaches
- web UI/UX for repository-native agent workflows
- knowledge promotion/demotion logic ("bubbling and sinking")
- background jobs, automation, and testing
- product thinking around what an AI-native SCM/workbench should actually be

If you are interested in version control, SQLite, local-first tools, knowledge systems, or agent tooling with auditable provenance, I would like to talk.

Repository docs are in `doc/ai/` and `doc/specs/` in this repo. If there is interest, I can also write up a more concrete architecture note showing the current implementation, the missing pieces, and the contributor roadmap.

## Shorter HN version

I’m looking for developers to help build an AI-native software development environment on top of Fossil SCM.

The thesis is that the repository should be more than code storage: it should also hold context, rationale, retrieval metadata, and durable project knowledge. Fossil is a good base because it is already a structured SQLite repository with versioning, wiki, chat, tickets, and a built-in web UI.

This fork explores:

- provenance-aware agent actions
- automatic micro-commits
- tiered knowledge capture
- semantic retrieval / RAG over repo knowledge
- a self-maintaining memory model where useful concepts bubble up and stale ones sink

There is already early work in the repo for AI tables, commit provenance, note storage, semantic indexing, and a local `/agentui`.

I’m especially interested in contributors with experience in C, SQLite, retrieval/vector search, Fossil internals, and web UI for serious developer tools.

If that sounds interesting, reply here.

## Notes for posting

- Prefer the shorter version for HN if you want more replies.
- Prefer the longer version if you expect readers to ask "what is actually implemented?"
- If you want, this can be followed by a separate technical post focused only on architecture and contributor tasks.
