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
  Provider/model design notes live in
  [`doc/ai/PROVIDER_MODEL_SPLIT.md`](doc/ai/PROVIDER_MODEL_SPLIT.md) and
  [`doc/ai/IMPLEMENTATION_PLAN.md`](doc/ai/IMPLEMENTATION_PLAN.md), which now
  includes the phased roadmap from config visibility through streaming and
  conversation evaluation. Tcl test suite recovery is tracked in
  [`doc/ai/TCL_TEST_REVIVAL_PLAN.md`](doc/ai/TCL_TEST_REVIVAL_PLAN.md).
- Clean local reinstall helper: [`dev/tools/install-fossil-clean.sh`](dev/tools/install-fossil-clean.sh)
- Tcl test prerequisite helper:
  [`dev/tools/install-tcl-test-prereqs.sh`](dev/tools/install-tcl-test-prereqs.sh)

## AI Agent Configuration

The local agent integration supports separate models for chat and embeddings.

Checkout-local config lives in [`cfg/ai-agent.json`](cfg/ai-agent.json):

```json
{
  "provider": "ollama",
  "model": "qwen3.5:0.8b",
  "command": "/home/you/.config/fossil/agents/fossil-ollama-agent.sh",
  "embedding_provider": "ollama",
  "embedding_model": "mxbai-embed-large",
  "embedding_command": ""
}
```

Notes:

- `provider` selects the chat backend. Current built-in compatibility values are
  `ollama`, `codex`, and `custom`.
- `model` is the chat model used by `/agentui` and `/agent-chat`.
- `embedding_provider` selects the embedding backend independently from chat.
- `embedding_model` is used by `fossil agent embed`, `semantic-index`, and `retrieve`.
- Maintained helper scripts live in [`dev/agents/fossil-ollama-agent.sh`](dev/agents/fossil-ollama-agent.sh)
  and [`dev/agents/fossil-codex-agent.sh`](dev/agents/fossil-codex-agent.sh).
- `embedding_command` may be left empty to use Ollama's HTTP `/api/embed` fallback.
- `qwen3.5:0.8b` does not provide embeddings in Ollama, so a separate embedding model is required.
- When `provider` or `embedding_provider` is omitted, Fossil infers it from the
  configured command for compatibility with older configs.
- Fossil rejects obvious provider/model mismatches before launching the backend,
  such as `provider=codex` with an Ollama-style model name or `provider=ollama`
  with `model=auto`.
- `/agentui` stores the effective provider/model with each chat session and
  restores that pair when an existing session is reopened.
- `/agent-config` exposes the effective chat and embedding config as JSON for
  `/agentui` and tests.
- `/agent-config` also reports current backend capability flags such as
  provider locking, streaming support, model discovery support, and whether
  embeddings are currently available.
- `/agent-config` now also includes static provider choices and model
  suggestions so `/agentui` can populate controls from server-declared data.
- chat rows now persist a structured `kind` classification such as `prompt`,
  `reply`, `error`, or `progress`, which is the first step toward structured
  chat events.
- `/agent-history` exposes a stored chat session and its ordered messages as
  JSON, providing a structured read path for future UI work. `/agentui` now
  uses this endpoint for browser-side history rendering.
- chat rows now also support a lightweight `meta` field for structured event
  metadata such as whether context assembly was enabled for a prompt.
- Runtime config lookup order is: `--agent-config`, `FOSSIL_AGENT_CONFIG`,
  repo setting `agent-config-path`, user config
  `${XDG_CONFIG_HOME:-$HOME/.config}/fossil/ai-agent.json`, then checkout-local
  `cfg/ai-agent.json`, then repo settings such as `agent-command`,
  `agent-model`, `agent-provider`, `agent-embedding-model`, and
  `agent-embedding-provider`.
- To point Fossil at a shared config file, set `agent-config-path` in the
  repository, pass `fossil agent --agent-config /absolute/path/to/fossil-agent.json ...`,
  or export `FOSSIL_AGENT_CONFIG=/absolute/path/to/fossil-agent.json`.
- For Codex-backed chat, use `fossil-codex-agent.sh` and set `"model": "auto"`
  unless your Codex account supports an explicit model name.
- `make install` also creates `${XDG_CONFIG_HOME:-$HOME/.config}/fossil/agents`
  and populates it with `ai-agent.json`, `ai-agent-codex.json`, and the agent
  wrapper scripts when `DESTDIR` is empty. It also installs the default
  Ollama config at `${XDG_CONFIG_HOME:-$HOME/.config}/fossil/ai-agent.json`.
- When `make install` runs under `sudo`, the config skeleton is written to the
  invoking user's config directory rather than `/root/.config/fossil`, and the
  installed config files are owned by that invoking user.

## Testing

`make test` runs the Tcl regression suite through
[`tst/tester.tcl`](tst/tester.tcl).

- Core AI data-pool tests: [`tst/ai.test`](tst/ai.test)
- Hermetic agent regression tests: [`tst/agent.test`](tst/agent.test)
- Fake backend fixture: [`tst/fake-agent-backend.sh`](tst/fake-agent-backend.sh)

The agent regression tests are deterministic and do not require Ollama,
Codex, or network access. They cover:

- AI schema initialization and self-test review loop
- `agent note`, `agent embed`, `semantic-index`, and `retrieve`
- user-config and repo `agent-config-path` resolution
- first-use `/agentui` rendering
- `/agent-config` JSON for effective provider/model/config state
- `/agent-config` capability flags for the active backend
- `/agent-config` provider choices and model suggestions
- `/agent-history` JSON for stored sessions and ordered messages
- structured chat event kinds in `agentchat`
- lightweight structured `meta` on `agentchat` rows
- first-use `/agent-chat` session creation and message persistence
- effective chat and embedding model display in `/agentui`
- provider/model persistence across reopened chat sessions

Some Tcl tests are intentionally feature-gated and will report as skipped
instead of failed when their prerequisites are unavailable.

Common skip prerequisites:

- `json`: Fossil must be built with JSON support and Tcl must have the
  `json` package from Tcllib installed.
- `set-manifest` and `unversioned`: Tcl must have the `sha1` package from
  Tcllib installed.
- `th1-docs`: Fossil must be built with TH1 docs support and Tcl support.
- `th1-hooks`: Fossil must be built with TH1 hooks support.
- `th1-tcl`: Fossil must be built with Tcl support.
- `merge5`: intentionally disabled until its legacy fixture is repaired for
  current `fossil sqlite3 --no-repository` behavior.

The Tcl runner prints skip reasons in the final summary so a developer can
distinguish optional-environment skips from real regressions.

To check or install the optional Tcllib packages used by the skipped tests:

```bash
dev/tools/install-tcl-test-prereqs.sh --check
dev/tools/install-tcl-test-prereqs.sh --print
```
