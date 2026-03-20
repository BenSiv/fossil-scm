# Fossil AI Agent vs. AgentOps 2026

The AI integration in Fossil SCM is a sophisticated "AgentOps" implementation designed to be dependency-free and local-first.

## 1. Orchestration & Frameworks (The "Brain")
Fossil uses a **TH1-based Orchestration Engine**. It handles the full agent lifecycle:
- **Stateful Control**: Session history persisted in repository tables.
- **Context Gathering**: Automatic assembly of repository structure, pending changes, and RAG notes.
- **Reasoning**: Built-in parsing for thinking/reasoning tags.

## 2. Tool Integration Platforms (The "Hands")
Fossil provides native "skills" and a universal "USB port" for AI:
- **Built-in Tools**: `repomap`, `changes`, `wiki-sync`.
- **MCP Support**: A native Model Context Protocol (MCP) server enables interaction with external agentic ecosystems.

## 3. Observability & AgentOps (The "Eyes")
- **Trace History**: Every reasoning step is recorded in the repo.
- **Evaluation**: Built-in feedback loops (`useful`/`not-useful`) and quality reporting.

## 4. Evaluation & Guardrails (The "Safety")
- **Tiered Knowledge**: A multi-tier note pool (T0-T3) creates expert-grounded benchmarks.
- **Context Isolation**: The agent is isolated to the repository scope via TH1 guardrails.
