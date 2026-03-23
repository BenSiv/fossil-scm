#ifndef FOSSIL_AGENT_INTERNAL_H
#define FOSSIL_AGENT_INTERNAL_H

/*
** Private internal interfaces shared across the split agent implementation.
** This header is for Fossil's C files only.
*/

typedef struct AgentToolDef AgentToolDef;
struct AgentToolDef {
  const char *zName;
  const char *zDescription;
  const char *zKind;
  int bRequiresConfirm;
  int bBuiltin;
};

int agent_chat_session_create(
  const char *zUser,
  const char *zProvider,
  const char *zModel
);
void agent_chat_session_rename(int sid, const char *zTitle);
int agent_chat_session_exists(int sid);
int agent_chat_latest_session(const char *zUser);
int agent_chat_latest_terminal_acid(int sid);
int agent_chat_is_terminal_acid(int sid, int acid);
const char *agent_chat_session_state(int sid);
const char *agent_chat_session_request_id(int sid);
const char *agent_chat_session_request_state(int sid);
int agent_chat_session_request_count(int sid);
int agent_chat_save(
  int sid,
  const char *zUser,
  const char *zRole,
  const char *zKind,
  const char *zProvider,
  const char *zModel,
  const char *zMeta,
  const char *zMsg
);
void agent_chat_save_event(
  int sid,
  const char *zUser,
  const char *zKind,
  const char *zProvider,
  const char *zModel,
  const char *zMeta,
  const char *zMsg
);
void agent_chat_render_sessions_to_blob(
  const char *zUser,
  int sidCurrent,
  Blob *pOut
);
void agent_chat_render_history_to_blob(int sidCurrent, Blob *pOut);
void agent_emit_history_json(int sidCurrent);
void agent_emit_events_json(int sidCurrent, int afterAcid);
void agent_emit_session_list_json(const char *zUser);
void agent_emit_history_object_json(int sidCurrent);
void agent_emit_events_array_json(int sidCurrent, int afterAcid, int *pLastAcid);
void agent_emit_session_array_json(const char *zUser);
void agent_emit_active_request_ids_json(int sidCurrent);
void agent_emit_request_object_json(int sidCurrent, const char *zRequestId);
void agent_emit_latest_request_json(int sidCurrent);
const AgentToolDef *agent_tool_find(const char *zName);
void agent_emit_tool_json(const char *zName);
void agent_emit_tool_array_json(void);

int agent_request_create(int sid, const char *zRequestId, const char *zState);
void agent_request_set_state(int rid, const char *zState, int terminalAcid);

const char *agent_chat_session_model(int sid, const char *zDefault);
const char *agent_chat_session_provider(int sid, const char *zDefault);
const char *agent_default_model(void);
const char *agent_chat_provider(void);
const char *agent_embedding_provider(void);
const char *agent_embedding_model(void);
const char *agent_embedding_template(void);
const char *agent_command_template(void);
char *agent_config_source(void);
int agent_chat_provider_locked(void);
int agent_provider_accepts_auto(const char *zProvider);
int agent_provider_rejects_ollama_models(const char *zProvider);
void agent_emit_config_json(int sidCurrent);
void agent_console_submenu(int sidCurrent);
char *agent_config_get(const char *zKey);
void agent_expand_command(
  Blob *pOut,
  const char *zTemplate,
  const char *zModel
);
void agent_prepare_command(
  Blob *pOut,
  const char *zMode,
  const char *zProvider,
  const char *zModel,
  Blob *pCmd
);
void agent_strip_ansi(Blob *pText);
void agent_strip_prefix_noise(Blob *pText);
int agent_validate_provider_model(
  const char *zProvider,
  const char *zModel,
  Blob *pErr
);
int agent_assemble_context(
  Blob *pOut,
  const char *zModel,
  const char *zQuery,
  int *pRetrievalQid
);
void agent_sse_handler(const char *zChunk, int nChunk, void *pApp);
int agent_run_backend_core(
  const char *zProvider,
  const char *zModel,
  const char *zPrompt,
  Blob *pReply,
  Blob *pErr,
  void (*xChunk)(const char*, int, void*),
  void *pApp
);

int agent_load_asset(const char *zAsset, Blob *pOut);
const char *agent_orchestration_script(const char *zRole, Blob *pScript);

#endif
