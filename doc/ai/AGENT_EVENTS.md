# Agent Event Model

Scope: define the persisted event model used by `agentchat`, `/agent-history`,
`/agent-events`, and `/agentui`.

## Storage

Events are stored in `agentchat`.

Relevant columns:

- `acid` INTEGER PRIMARY KEY
- `sid` INTEGER REFERENCES `agentchat_session`
- `role` TEXT
- `kind` TEXT
- `provider` TEXT
- `model` TEXT
- `meta` TEXT
- `msg` TEXT

## Roles

- `user`
  - human-entered prompt text
- `system`
  - Fossil-generated status or tool events
- `agent`
  - backend-produced terminal outcome

## Kinds

Stable `kind` values currently used:

- `prompt`
  - user prompt row
- `tool`
  - backend invocation marker
- `progress`
  - in-flight or milestone status
- `reply`
  - final non-error backend answer
- `error`
  - terminal backend failure

New `kind` values should only be added when:

- the payload shape is documented
- `/agent-history`
- `/agent-events`
- `/agentui`
- Tcl tests

are updated together.

## Meta Contracts

Current `meta` payload contracts:

- prompt
  - `{"context":true|false}`
- tool
  - `{"tool":"chat-backend","provider":"..."}`
- progress: context assembly
  - `{"stage":"context","enabled":true}`
- progress: backend running
  - `{"stage":"backend","status":"running"}`
- progress: backend success
  - `{"stage":"backend","status":"ok"}`
- progress: backend failure
  - `{"stage":"backend","status":"error"}`

These are JSON strings stored in the `meta` column.

## Event Order

Successful context-aware chats currently persist:

1. `system/progress` for context assembly
2. `system/tool` for backend invocation
3. `system/progress` for backend running
4. `user/prompt`
5. `system/progress` for backend success
6. `agent/reply`

Failed backend runs replace step 6 with `agent/error` and step 5 uses
`status="error"`.

## UI Rules

- `/agent-history` returns the full ordered event stream for a session
- `/agent-events` returns ordered events, optionally filtered by `after=ACID`
- `/agentui`:
  - loads full history once
  - polls `/agent-events` incrementally
  - derives the status line from the newest event
  - derives session-summary state from the latest stored event in each session

## Non-Goals

This event model does not yet guarantee:

- token-by-token streaming
- hidden chain-of-thought visibility
- provider-specific structured tool calls
- full answer-evaluation semantics

Those must layer on top of this schema rather than bypass it.

User feedback is not stored as a new `agentchat.kind`. It currently lives in
`ai_chat_eval.user_feedback` and is joined onto `/agent-history` and
`/agent-events` as message metadata for terminal agent replies.
