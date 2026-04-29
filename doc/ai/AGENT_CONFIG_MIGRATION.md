# Agent Configuration Migration

Fossil no longer uses a separate agent JSON configuration resolver. Agent
runtime values now use the same configuration path as the rest of Fossil:
`fossil set`, `fossil unset`, optional `--global`, and versionable
`.fossil-settings/<setting>` files where the setting is marked versionable.

## What Changed

Removed config sources:

- `fossil agent --agent-config FILE`
- `FOSSIL_AGENT_CONFIG`
- `agent-config-path`
- `cfg/ai-agent.json`
- `.fossil-settings/ai-agent.json`
- `~/.config/fossil/ai-agent.json`

Use normal settings instead:

```bash
fossil set agent-provider codex
fossil set agent-model auto
fossil set agent-command /absolute/path/to/dev/agents/fossil-codex-agent.sh
fossil set agent-embedding-provider codex
fossil set agent-embedding-model text-embedding-3-small
fossil set agent-embedding-command /absolute/path/to/dev/agents/fossil-codex-embed.sh
fossil agent verify
```

Use global defaults like any other Fossil setting:

```bash
fossil set --global agent-provider ollama
fossil set --global agent-model qwen3.5:0.8b
```

## Setting Map

Old JSON key -> Fossil setting:

- `provider` -> `agent-provider`
- `model` -> `agent-model`
- `command` -> `agent-command`
- `embedding_provider` -> `agent-embedding-provider`
- `embedding_model` -> `agent-embedding-model`
- `embedding_command` -> `agent-embedding-command`
- `thinking_tag` -> `agent-thinking-tag`
- `chat_provider_locked` -> `agent-chat-provider-locked`
- `auto_promote_markdown` -> `agent-auto-promote-markdown`

Provider metadata such as model suggestions, `auto` validation, and embedding
fallback behavior is built into Fossil. There is no `providers` JSON catalog to
maintain.

## Versionable Values

Settings marked versionable can be shared through `.fossil-settings/SETTING`:

```bash
mkdir -p .fossil-settings
printf 'codex\n' > .fossil-settings/agent-provider
printf 'auto\n' > .fossil-settings/agent-model
printf 'codex\n' > .fossil-settings/agent-embedding-provider
printf 'text-embedding-3-small\n' > .fossil-settings/agent-embedding-model
```

Command settings are intentionally normal sensitive settings, not versioned
files. Set them locally or globally with `fossil set` so each user controls the
executable paths used on their machine.

## Verification

Check the effective configuration with:

```bash
fossil agent verify
fossil agent verify --json
```

The reported source is now `fossil settings`.
