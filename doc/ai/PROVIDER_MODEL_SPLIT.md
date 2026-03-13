# Provider/Model Split

This note captures design direction for Fossil's AI integration after testing
multiple backends and reviewing external systems with stronger provider/model
separation.

## Problem

Fossil currently treats the chat backend as a command template plus a free-form
model string.

That creates several avoidable failures:

- a model name can be valid for one backend and invalid for another
- the UI can show a model string without showing which backend will interpret it
- runtime fallback can silently switch behavior from user config to repo config
- errors are reported too late, only after invoking the backend

Examples seen during testing:

- `auto` is meaningful for the Codex wrapper but invalid for Ollama
- `qwen3.5:0.8b` is meaningful for Ollama but invalid for Codex
- a backend mismatch can look like a model lookup failure even when config
  resolution is the real problem

## Current Fossil Shape

Today Fossil effectively resolves:

- `command`
- `model`
- `embedding_model`

and then exports the model to the wrapper process.

This is pragmatic and keeps local integrations easy, but it leaves vendor
identity implicit.

## Direction

Fossil should separate:

- provider or vendor identity
- model identity within that provider
- transport or command implementation

Minimum conceptual model:

- `provider`
  Examples: `ollama`, `codex`, `openai-compatible`, `custom-command`
- `model`
  Interpreted only within the selected provider
- `embedding_provider`
- `embedding_model`

The command wrapper can remain an implementation detail of the provider.

## Recommended Runtime Behavior

For chat:

- resolve an effective provider
- resolve an effective model for that provider
- validate the pair before invoking the backend
- surface both values in the UI

For embeddings:

- treat embedding provider and embedding model independently from chat
- allow chat and retrieval to use different vendors

## UI Implications

`/agentui` should not present a plain free-form model string without showing the
effective backend.

The UI should surface:

- effective config source
- effective chat provider
- effective chat model
- effective embedding provider
- effective embedding model

Longer term, the UI should prefer provider-aware selection over raw text entry.

## Config Implications

Current config remains:

- `command`
- `model`
- `embedding_command`
- `embedding_model`

Recommended future config:

```json
{
  "provider": "codex",
  "model": "auto",
  "embedding_provider": "ollama",
  "embedding_model": "mxbai-embed-large"
}
```

For custom or advanced integrations, Fossil may still allow:

- explicit command overrides
- explicit binary overrides
- custom provider definitions

But provider identity should remain explicit even when a custom command is used.

## Command Wrappers

Wrapper scripts are still useful:

- local authentication setup
- stable CLI invocation
- environment normalization
- server environments with reduced `PATH`

But wrappers should sit beneath provider resolution, not replace it.

## Error Handling

Once provider and model are split, Fossil can reject obvious mismatches early:

- `provider=codex, model=qwen3.5:0.8b`
- `provider=ollama, model=auto`

This is better than deferring failure to backend process execution.

## Streaming Implications

Provider identity also matters for streaming.

Different providers expose progress differently:

- streamed text chunks
- SSE frames
- tool-call events
- provider metadata
- visible reasoning text

Fossil should treat streaming as a provider capability, not assume all providers
behave like a buffered shell command.

## Non-Goals

This direction does not require:

- copying another project's provider SDK structure
- building a full remote client/server architecture
- exposing every model-specific tuning parameter in Fossil immediately

The goal is clarity and correct behavior, not framework expansion.

## Immediate Next Steps

- keep existing wrapper-based integrations working
- add explicit provider metadata to config and UI
- show the effective resolved config source in `/agentui`
- validate provider/model pairs before backend execution
