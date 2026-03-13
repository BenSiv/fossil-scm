# About Fossil

Fossil is a distributed version control system that has been widely
used since 2007.  Fossil was originally designed to support the
[SQLite](https://sqlite.org) project but has been adopted by many other
projects as well.

Fossil is self-hosting at <https://fossil-scm.org>.

If you are reading this on GitHub, then you are looking at a Git mirror
of the self-hosting Fossil repository.  The purpose of that mirror is to
test and exercise Fossil's ability to export a Git mirror.  Nobody much
uses the GitHub mirror, except to verify that the mirror logic works.  If
you want to know more about Fossil, visit the official self-hosting site
linked above.

## Documentation

Canonical documents for this fork live under [`doc/`](doc/):

- Build Guide: [`doc/BUILD.txt`](doc/BUILD.txt)
- Licence: [`doc/LICENCE.md`](doc/LICENCE.md)
- Repository Map: [`doc/specs/repo-map.md`](doc/specs/repo-map.md)
- AI docs: [`doc/ai/`](doc/ai/)
  Includes the data pool strategy in [`doc/ai/DATA_POOL.md`](doc/ai/DATA_POOL.md).

## AI Agent Configuration

The local agent integration supports separate models for chat and embeddings.

Checkout-local config lives in [`cfg/ai-agent.json`](cfg/ai-agent.json):

```json
{
  "model": "qwen3.5:0.8b",
  "command": "/absolute/path/to/fossil-scm/dev/agents/fossil-ollama-agent.sh",
  "embedding_model": "mxbai-embed-large",
  "embedding_command": ""
}
```

Notes:

- `model` is the chat model used by `/agentui` and `/agent-chat`.
- `embedding_model` is used by `fossil agent embed`, `semantic-index`, and `retrieve`.
- Maintained helper scripts live in [`dev/agents/fossil-ollama-agent.sh`](dev/agents/fossil-ollama-agent.sh)
  and [`dev/agents/fossil-codex-agent.sh`](dev/agents/fossil-codex-agent.sh).
- `embedding_command` may be left empty to use Ollama's HTTP `/api/embed` fallback.
- `qwen3.5:0.8b` does not provide embeddings in Ollama, so a separate embedding model is required.
- When Fossil serves a bare `.fossil` repository file, repo settings such as `agent-command`, `agent-model`, and `agent-embedding-model` apply. `cfg/ai-agent.json` is only visible from an open checkout.
- To point Fossil at a shared config file, set `agent-config-path` in the
  repository, pass `fossil agent --agent-config /absolute/path/to/fossil-agent.json ...`,
  or export `FOSSIL_AGENT_CONFIG=/absolute/path/to/fossil-agent.json`.
- For Codex-backed chat, use `fossil-codex-agent.sh` and set `"model": "auto"`
  unless your Codex account supports an explicit model name.
