# Fossil AI User Guide: The Autonomous Agent

The Fossil AI Agent is a local-first, dependency-free AgentOps environment integrated directly into Fossil SCM. It uses a **Knowledge Graph** and **Tiered Memory** system to provide context-aware assistance and autonomous editing.

---

## 🚀 1. Getting Started

### Activation
AI features are disabled by default. Use the following commands to initialize the required tables and enable the agent:
```bash
fossil ai enable
fossil ai init
```

### Configuration
The agent's behavior is governed by **TH1 Role Scripts**. You can find the default orchestration logic in:
- `cfg/roles/default.th1`: The main reasoning loop.
- `cfg/agent_prompts.json`: Natural language fragments for system prompts.

---

## 🧠 2. The Knowledge Lifecycle

Fossil uses a "Tiered" memory system (T0–T3) that automatically promotes useful information based on frequency and quality.

### Adding Knowledge (`fossil agent note`)
You can feed the agent specific facts, architectural decisions, or "gotchas":
```bash
fossil agent note "The build system requires out/autoconfig.h" --tier 2 --title "Build Requirements"
```
- **T0 (Raw)**: Default for fleeting notes.
- **T1 (Working)**: Information you use daily.
- **T2 (Draft)**: Refined, multi-note summaries.
- **T3 (Atomic)**: Canonical "Ground Truth" for the project.

### Semantic Indexing
To keep the search index up to date with new notes or wiki changes, run:
```bash
fossil agent semantic-index
```

### Knowledge Graph & Expansion
The agent automatically builds a **Knowledge Graph** based on your interactions. 
- If Note A and Note B are often retrieved together, they become "linked."
- During retrieval, if the agent finds a relevant note, it automatically "follows the links" to pull in related context that vector search might have missed.

---

## 💬 3. Interacting with the Agent

### The Chat UI
Navigate to `/agent-chat` in your Fossil UI to start a real-time streaming conversation.

### Collective Synthesis
When you ask a complex question, the agent performs a **Synthesis Phase**:
1. It retrieves raw notes and code diffs.
2. It uses the **Knowledge Graph** to find related info.
3. It performs a "pre-reasoning" step to consolidate these fragments into a single, unified "Context Briefing" before giving you a final answer.

---

## 🛠️ 4. Autonomous Actions & HITL

The agent can perform actions on your repository, but it follows a strict **Human-in-the-Loop (HITL)** safety workflow for dangerous tools like `edit_file`.

### Proposing an Edit
When the agent wants to change a file, it will:
1. Generate a **Propose Edit** payload.
2. Intercept the call and show you a **Side-by-Side Diff** in the UI.
3. Wait for your **Approval** or **Rejection**.

### Executing the Action
Only after you click "Approve" will the agent send the `CONFIRMED_EDIT` signal back to the C-core to actually write the bits to disk.

---

## 🔧 5. Extending the Agent

### Adding MCP Tools
Fossil supports the **Model Context Protocol (MCP)**. You can add new tools by editing:
- `cfg/mcp_tools.json`: Define the tool's schema and parameters.
- Add a handler in `src/agent.c` (or via a TH1 script) to execute the tool logic.

### Custom Roles
Create new TH1 scripts in `cfg/roles/` to define specialized agents (e.g., `reviewer.th1`, `tester.th1`) with their own system prompts and tool constraints.
