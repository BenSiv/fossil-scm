# AI Agent Architecture: TH1 Orchestration Layer

## Overview

The Fossil AI Agent uses a **"Muscle and Brain"** architecture. The C core provides the "Muscle" (vector math, database access, process execution), while a TH1 (Fossil's Tcl dialect) scripting layer provides the "Brain" (orchestration, prompt construction, response parsing).

This approach maintains Fossil's "Single Binary" philosophy while providing the flexibility and safety needed for dynamic AI interactions.

## Why TH1 Glue?

1.  **Safety**: TH1 is an memory-safe scripting language. Handling complex JSON payloads and XML-like tags (reasoning/thinking) in C is error-prone.
2.  **Flexibility**: AI prompts and response formats evolve rapidly. Moving this logic to scripts allows for rapid iteration without recompiling the C binary.
3.  **Fossil-Native**: TH1 is integrated directly into Fossil. It requires zero external dependencies (no Node.js, Python, etc.).
4.  **Deployment**: Scripts can be stored as **Unversioned Content** inside the repository database, allowing them to stay "with the data" and synchronize between clones.

## Component Responsibilities

### C Core (Muscle)
- **`agent_context`**: Gathers repository state (diffs, file maps, recent commits) into a structured blob.
- **`agent_run`**: Manages the low-level child process execution (substituting variables, handling timeouts).
- **`agent_vector_search`**: High-performance semantic search against the vector indices.
- **`agent_chat_save_event`**: Atomic persistence of structured events to the SQLite store.

### TH1 Layer (Brain/Glue)
- **Prompt Engineering**: Composing the system and user prompts with the appropriate context.
- **Response Parsing**: Splitting a single LLM stream into `<thought>` and `<reply>` components.
- **Fallbacks & Retries**: Implementing logic to handle provider-specific errors or empty responses.
- **Formatting**: Preparing the final output for the `/agentui` frontend.

## Data Flow

1.  **Request**: User sends a msg to `/agent-chat`.
2.  **Initialization**: C core initializes a TH1 interpreter.
3.  **Evaluation**: C core evaluates the orchestration script (e.g., `agent_orchestrate.th1`).
4.  **Interaction**: The script calls C-exposed functions (`agent_context`, `agent_run`) to perform work.
5.  **Persistence**: The script uses C-exposed save functions to record the interaction.
6.  **Response**: The script returns a formatted result to C, which then sends it to the client.

## Storage Options

Orchestration scripts and complex configurations can be stored in three ways:
1.  **Built-in**: Compiled into the Fossil binary (as `.h` headers via the `mkbuiltin` tool).
2.  **Unversioned (UV)**: Stored in the repo DB, allowing for per-repository overrides.
3.  **Local Files**: For developer testing and rapid prototyping.

## Migration Paths

Existing hardcoded C logic in `src/agent.c` (like `agent_chat_page`) will be incrementally refactored to call out to TH1 equivalents.
