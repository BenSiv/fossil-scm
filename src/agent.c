/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
** This program is distributed in the hope that it will be useful,
** but without even the implied warranty of merchantability or
** fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code to implement the "agent" command.
*/
#include "config.h"
#include "agent.h"
#include <assert.h>
#ifdef FOSSIL_ENABLE_JSON
#include "cson_amalgamation.h"
#include "json_detail.h"
#endif

int ai_is_enabled(void);
void ai_schema_ensure(void);
void ai_require_enabled(void);
int ai_note_create(
  int tier,
  const char *zTitle,
  Blob *pBody,
  const char *zSourceType,
  int sourceId,
  const char *zSourceRef,
  const char *zProcessLevel,
  const char *zMetadata,
  const char *zArtifactKind,
  const char *zArtifactRef,
  int artifactRid,
  const char *zArtifactPath,
  const char *zArtifactStatus
);
int ai_retrieval_begin(int contextId, const char *zQuery);
double ai_note_record_retrieval(
  int qid,
  int nid,
  int rank,
  double score,
  double tierWeight
);
void ai_retrieval_review(int qid);
void ai_chat_eval_record(
  int sid,
  int acid,
  const char *zProvider,
  const char *zModel,
  const char *zKind,
  const char *zMsg
);
char *ai_note_related_nids(int nid, int limit);
double ai_note_authority_score(int nid);
void ai_chat_eval_feedback(int sid, int acid, const char *zFeedback);

static int agent_generate_embedding(
  const char *zModel,
  const char *zText,
  Blob *pOut
);
static const char *agent_chat_session_model(int sid, const char *zDefault);
static const char *agent_chat_session_provider(int sid, const char *zDefault);
static const char *agent_command_template(void);
static const char *agent_embedding_template(void);
static char *agent_command_executable(const char *zCmdTmpl);
#ifdef FOSSIL_ENABLE_JSON
static cson_object *agent_config_parse_object(cson_value **ppRoot, Blob *pJson);
static cson_object *agent_config_parse_object_path(
  const char *zPath,
  cson_value **ppRoot,
  Blob *pJson
);
static const char *agent_json_cstr(cson_object *pObj, const char *zKey);
static cson_array *agent_provider_array(cson_object *pRootObj);
static cson_object *agent_metadata_parse_object(
  cson_value **ppRoot,
  Blob *pJson
);
static int agent_provider_match_command(
  cson_object *pProvider,
  const char *zCmd,
  const char *zCmdExec
);
#endif
static void agent_strip_prefix_noise(Blob *pText);
static int agent_validate_provider_model(
  const char *zProvider,
  const char *zModel,
  Blob *pErr
);
static int agent_run_backend(
  const char *zProvider,
  const char *zModel,
  const char *zPrompt,
  Blob *pReply,
  Blob *pErr
);
static int agent_phase_registry_count(void);
static void agent_emit_phase_registry_json(void);
static int agent_capability_registry_count(void);
static void agent_emit_capability_registry_json(void);
static void agent_run_create_tables(void);

typedef struct AgentRecipe AgentRecipe;
typedef struct AgentPhase AgentPhase;
typedef struct AgentCapability AgentCapability;
struct AgentRecipe {
  const char *zName;
  const char *zTitle;
  const char *zDescription;
  const char *zUsage;
  const char *zGuidanceRefs;
  const char *zPhases;
  const char *zCapabilities;
  const char *zScript;
};
struct AgentPhase {
  const char *zName;
  const char *zTitle;
  const char *zDescription;
};
struct AgentCapability {
  const char *zName;
  const char *zKind;
  const char *zDescription;
  int requiresWrite;
  int requiresNetwork;
  int requiresConfirm;
};

/*
** Repo-local config file for agent integration. When present, this file
** overrides the corresponding Fossil settings for the agent runtime.
*/
static const char zAgentConfigFile[] = "cfg/ai-agent.json";
static const char *zAgentConfigPath = 0;
static int agentLastRetrievalQid = 0;

/*
** SETTING: agent-command width=60
**
** Shell command template used by /agent-chat to invoke an AI backend.
** The selected model name is substituted for "%m" (shell-escaped). If
** "%m" does not appear, the command is used as-is and the model remains
** available to wrappers via the FOSSIL_AGENT_MODEL environment variable.
*/
/*
** SETTING: agent-config-path width=80
**
** Optional path to a JSON config file used to override the default
** checkout-local cfg/ai-agent.json lookup. This is especially useful when
** serving a bare repository file or when multiple working trees share a
** single agent config.
*/
/*
** SETTING: agent-model width=30
**
** Default model name used by /agent-chat when the request does not
** specify a model explicitly.
*/
/*
** SETTING: agent-provider width=20
**
** Optional explicit provider name for chat requests. Examples include
** "ollama", "codex", and "custom". If unset, the provider is inferred
** from the configured command template for compatibility.
*/
/*
** SETTING: agent-embedding-command width=80
**
** Optional shell command template used to generate embeddings. The text to
** embed is sent to stdin and "%m" is substituted with the selected model. If
** this setting is empty, embedding-based semantic search is disabled unless
** legacy Ollama settings are present.
*/
/*
** SETTING: agent-embedding-model width=30
**
** Default model name used for embedding generation and retrieval. If unset,
** the chat model is reused.
*/
/*
** SETTING: agent-embedding-provider width=20
**
** Optional explicit provider name for embeddings. If unset, the provider is
** inferred from embedding-command or falls back to Ollama's HTTP API when
** embedding-command is empty.
*/
/*
** SETTING: agent-ollama-command width=40 default=ollama
**
** Legacy compatibility setting. Used only when agent-command is unset.
*/
/*
** SETTING: agent-ollama-model width=20
**
** Legacy compatibility setting. Used only when agent-model is unset.
*/
/*
** SETTING: agent-history-count width=10 default=50
**
** Number of recent agent chat messages to render in /agentui.
*/

/*
** Expand zTemplate into pOut. Replaces %m with the shell-escaped model name
** and %% with a literal percent sign.
*/
static void agent_expand_command(
  Blob *pOut,
  const char *zTemplate,
  const char *zModel
){
  const char *z = zTemplate;
  blob_zero(pOut);
  while( z && z[0] ){
    if( z[0]=='%' && z[1]=='m' ){
      blob_append_escaped_arg(pOut, zModel ? zModel : "", 0);
      z += 2;
    }else if( z[0]=='%' && z[1]=='%' ){
      blob_append(pOut, "%", 1);
      z += 2;
    }else{
      blob_append(pOut, z, 1);
      z++;
    }
  }
}

/*
** Return a freshly-allocated absolute path to cfg/ai-agent.json if the
** current process has an open checkout root. The caller must fossil_free()
** the result.
*/
#ifdef FOSSIL_ENABLE_JSON
static char *agent_user_config_path(void){
#if defined(_WIN32)
  return 0;
#else
  const char *zXdg = fossil_getenv("XDG_CONFIG_HOME");
  const char *zHome = fossil_getenv("HOME");
  if( zXdg && zXdg[0] ){
    return mprintf("%s/fossil/ai-agent.json", zXdg);
  }
  if( zHome && zHome[0] ){
    return mprintf("%s/.config/fossil/ai-agent.json", zHome);
  }
  return 0;
#endif
}

static char *agent_config_source(void){
  const char *zPath = fossil_getenv("FOSSIL_AGENT_CONFIG");
  char *zUserPath = 0;
  if( zAgentConfigPath && zAgentConfigPath[0] ){
    return mprintf("cli --agent-config: %s", zAgentConfigPath);
  }
  if( zPath && zPath[0] ){
    return mprintf("env FOSSIL_AGENT_CONFIG: %s", zPath);
  }
  if( g.repositoryOpen ){
    zPath = db_get("agent-config-path", 0);
    if( zPath && zPath[0] ){
      return mprintf("repo agent-config-path: %s", zPath);
    }
  }
  zUserPath = agent_user_config_path();
  if( zUserPath && file_size(zUserPath, ExtFILE)>=0 ){
    return mprintf("user config: %s", zUserPath);
  }
  fossil_free(zUserPath);
  if( g.zLocalRoot && g.zLocalRoot[0] ){
    return mprintf("checkout config: %s%s", g.zLocalRoot, zAgentConfigFile);
  }
  return mprintf("repo settings fallback");
}

static char *agent_config_path(void){
  const char *zPath = fossil_getenv("FOSSIL_AGENT_CONFIG");
  char *zUserPath = 0;
  if( zAgentConfigPath && zAgentConfigPath[0] ) return mprintf("%s", zAgentConfigPath);
  if( zPath && zPath[0] ) return mprintf("%s", zPath);
  if( g.repositoryOpen ){
    zPath = db_get("agent-config-path", 0);
    if( zPath && zPath[0] ) return mprintf("%s", zPath);
  }
  zUserPath = agent_user_config_path();
  if( zUserPath && file_size(zUserPath, ExtFILE)>=0 ) return zUserPath;
  fossil_free(zUserPath);
  if( g.zLocalRoot==0 || g.zLocalRoot[0]==0 ) return 0;
  return mprintf("%s%s", g.zLocalRoot, zAgentConfigFile);
}
#else
static char *agent_config_source(void){
  return mprintf("repo settings fallback (JSON disabled)");
}
#endif

static const char *agent_infer_provider(const char *zCmd){
#ifdef FOSSIL_ENABLE_JSON
  cson_value *pRoot = 0;
  Blob json = BLOB_INITIALIZER;
  cson_object *pRootObj = 0;
  cson_array *pProviders = 0;
  char *zCmdExec = 0;
  unsigned int i, n = 0;
  static char *zCached = 0;
#endif
  if( zCmd==0 || zCmd[0]==0 ) return "unset";
#ifdef FOSSIL_ENABLE_JSON
  pRootObj = agent_metadata_parse_object(&pRoot, &json);
  pProviders = agent_provider_array(pRootObj);
  zCmdExec = agent_command_executable(zCmd);
  n = pProviders ? cson_array_length_get(pProviders) : 0;
  fossil_free(zCached);
  for(i=0; i<n; i++){
    cson_value *pVal = cson_array_get(pProviders, i);
    cson_object *pProvider = cson_value_is_object(pVal)
      ? cson_value_get_object(pVal) : 0;
    const char *zName = agent_json_cstr(pProvider, "name");
    if( zName && agent_provider_match_command(pProvider, zCmd, zCmdExec) ){
      zCached = mprintf("%s", zName);
      break;
    }
  }
  fossil_free(zCmdExec);
  cson_value_free(pRoot);
  blob_reset(&json);
  if( zCached ) return zCached;
#endif
  return "custom";
}

/*
** Return non-zero if zModel looks like an Ollama-style local model name.
*/
static int agent_model_looks_ollama(const char *zModel){
  static const char *const azPrefix[] = {
    "llama", "qwen", "mxbai", "deepseek", "phi", "gemma", "nomic"
  };
  int i;
  if( zModel==0 || zModel[0]==0 ) return 0;
  if( strchr(zModel, ':')!=0 ) return 1;
  for(i=0; i<(int)(sizeof(azPrefix)/sizeof(azPrefix[0])); i++){
    size_t n = strlen(azPrefix[i]);
    if( fossil_strnicmp(zModel, azPrefix[i], (int)n)==0 ) return 1;
  }
  return 0;
}

#ifdef FOSSIL_ENABLE_JSON
/*
** Parse the config file at zPath and return the JSON root object on success.
** The caller must free *ppRoot and reset pJson when done.
*/
static cson_object *agent_config_parse_object_path(
  const char *zPath,
  cson_value **ppRoot,
  Blob *pJson
){
  cson_parse_info pinfo = cson_parse_info_empty;
  cson_value *pRoot = 0;
  cson_object *pObj = 0;
  if( ppRoot ) *ppRoot = 0;
  blob_zero(pJson);
  if( zPath==0 ) return 0;
  if( file_size(zPath, ExtFILE)<0 ){
    return 0;
  }
  if( blob_read_from_file(pJson, zPath, ExtFILE)<0 ){
    blob_reset(pJson);
    return 0;
  }
  pRoot = cson_parse_Blob(pJson, &pinfo);
  if( pRoot==0 || !cson_value_is_object(pRoot) ){
    cson_value_free(pRoot);
    blob_reset(pJson);
    return 0;
  }
  pObj = cson_value_get_object(pRoot);
  if( ppRoot ) *ppRoot = pRoot;
  return pObj;
}

/*
** Parse the current agent config file and return the JSON root object on
** success. The caller must free *ppRoot and reset pJson when done.
*/
static cson_object *agent_config_parse_object(
  cson_value **ppRoot,
  Blob *pJson
){
  char *zPath = agent_config_path();
  cson_object *pObj = agent_config_parse_object_path(zPath, ppRoot, pJson);
  fossil_free(zPath);
  return pObj;
}

/*
** Return the path to the shared provider-metadata config. If the active
** config is itself ai-agent.json, return it. Otherwise, prefer a sibling
** ai-agent.json and fall back to the active config path.
*/
static char *agent_metadata_config_path(void){
  char *zConfig = agent_config_path();
  char *zMeta = 0;
  char *zDir = 0;
  const char *zBase = 0;
  if( zConfig==0 ) return 0;
  zBase = file_tail(zConfig);
  if( fossil_strcmp(zBase, "ai-agent.json")==0 ){
    return zConfig;
  }
  zDir = file_dirname(zConfig);
  zMeta = mprintf("%s/ai-agent.json", zDir);
  fossil_free(zDir);
  if( file_size(zMeta, ExtFILE)>=0 ){
    fossil_free(zConfig);
    return zMeta;
  }
  fossil_free(zMeta);
  return zConfig;
}

/*
** Parse the shared provider-metadata config when available.
*/
static cson_object *agent_metadata_parse_object(
  cson_value **ppRoot,
  Blob *pJson
){
  char *zPath = agent_metadata_config_path();
  cson_object *pObj = agent_config_parse_object_path(zPath, ppRoot, pJson);
  fossil_free(zPath);
  return pObj;
}

static const char *agent_json_cstr(cson_object *pObj, const char *zKey){
  cson_value *pVal = pObj ? cson_object_get(pObj, zKey) : 0;
  return pVal ? cson_value_get_cstr(pVal) : 0;
}

static int agent_json_bool(cson_object *pObj, const char *zKey, int dflt){
  cson_value *pVal = pObj ? cson_object_get(pObj, zKey) : 0;
  if( pVal==0 ) return dflt;
  return cson_value_get_bool(pVal) ? 1 : (cson_value_get_integer(pVal)!=0);
}

static cson_array *agent_provider_array(cson_object *pRootObj){
  cson_value *pVal = pRootObj ? cson_object_get(pRootObj, "providers") : 0;
  return (pVal && cson_value_is_array(pVal)) ? cson_value_get_array(pVal) : 0;
}

static cson_object *agent_provider_object_from_array(
  cson_array *pProviders,
  const char *zProvider
){
  unsigned int i, n;
  if( pProviders==0 || zProvider==0 || zProvider[0]==0 ) return 0;
  n = cson_array_length_get(pProviders);
  for(i=0; i<n; i++){
    cson_value *pVal = cson_array_get(pProviders, i);
    cson_object *pObj = cson_value_is_object(pVal) ? cson_value_get_object(pVal) : 0;
    const char *zName = agent_json_cstr(pObj, "name");
    if( zName && fossil_strcmp(zName, zProvider)==0 ){
      return pObj;
    }
  }
  return 0;
}

static cson_object *agent_provider_object(
  cson_object *pRootObj,
  const char *zProvider
){
  return agent_provider_object_from_array(agent_provider_array(pRootObj), zProvider);
}

static int agent_provider_match_command(
  cson_object *pProvider,
  const char *zCmd,
  const char *zCmdExec
){
  const char *zName = agent_json_cstr(pProvider, "name");
  const char *zProviderCmd = agent_json_cstr(pProvider, "command");
  char *zProviderExec = 0;
  int rc = 0;
  if( zCmd==0 || zCmd[0]==0 ) return 0;
  if( zName && zName[0] ){
    Blob needle = BLOB_INITIALIZER;
    blob_appendf(&needle, " %s", zName);
    if( strstr(zCmd, blob_str(&needle))!=0
     || fossil_strncmp(zCmd, zName, (int)strlen(zName))==0
    ){
      blob_reset(&needle);
      return 1;
    }
    blob_reset(&needle);
  }
  if( zProviderCmd==0 || zProviderCmd[0]==0 ) return 0;
  zProviderExec = agent_command_executable(zProviderCmd);
  if( zProviderExec && zCmdExec
   && fossil_strcmp(file_tail(zProviderExec), file_tail(zCmdExec))==0
  ){
    rc = 1;
  }
  fossil_free(zProviderExec);
  return rc;
}

static int agent_provider_array_count(cson_object *pRootObj){
  cson_array *pProviders = agent_provider_array(pRootObj);
  return pProviders ? (int)cson_array_length_get(pProviders) : 0;
}

static const char *agent_provider_string_property(
  const char *zProvider,
  const char *zKey
){
  cson_value *pRoot = 0;
  Blob json = BLOB_INITIALIZER;
  cson_object *pRootObj = agent_metadata_parse_object(&pRoot, &json);
  cson_object *pProvider = agent_provider_object(pRootObj, zProvider);
  const char *zVal = agent_json_cstr(pProvider, zKey);
  static char *zCached = 0;
  fossil_free(zCached);
  zCached = zVal && zVal[0] ? mprintf("%s", zVal) : 0;
  cson_value_free(pRoot);
  blob_reset(&json);
  return zCached;
}

static int agent_provider_bool_property(
  const char *zProvider,
  const char *zKey,
  int dflt
){
  cson_value *pRoot = 0;
  Blob json = BLOB_INITIALIZER;
  cson_object *pRootObj = agent_metadata_parse_object(&pRoot, &json);
  cson_object *pProvider = agent_provider_object(pRootObj, zProvider);
  int rc = agent_json_bool(pProvider, zKey, dflt);
  cson_value_free(pRoot);
  blob_reset(&json);
  return rc;
}
#else
static const char *agent_provider_string_property(
  const char *zProvider,
  const char *zKey
){
  (void)zProvider;
  (void)zKey;
  return 0;
}

static int agent_provider_bool_property(
  const char *zProvider,
  const char *zKey,
  int dflt
){
  (void)zProvider;
  (void)zKey;
  return dflt;
}
#endif

/*
** Look up a string value in cfg/ai-agent.json. Returns a newly allocated
** string on success or NULL if the config file/key is missing or invalid.
** The caller must fossil_free() the result.
*/
static char *agent_config_get(const char *zKey){
#ifdef FOSSIL_ENABLE_JSON
  Blob json = BLOB_INITIALIZER;
  cson_value *pRoot = 0;
  cson_object *pObj = 0;
  cson_value *pVal = 0;
  const char *zVal = 0;
  char *zOut = 0;
  pObj = agent_config_parse_object(&pRoot, &json);
  if( pObj==0 ) return 0;
  pVal = cson_object_get(pObj, zKey);
  zVal = pVal ? cson_value_get_cstr(pVal) : 0;
  if( zVal && zVal[0] ){
    zOut = mprintf("%s", zVal);
  }
  cson_value_free(pRoot);
  blob_reset(&json);
  return zOut;
#else
  (void)zKey;
  return 0;
#endif
}

/*
** Return the configured chat model, with legacy fallback.
*/
static const char *agent_default_model(void){
  static char *zCached = 0;
  fossil_free(zCached);
  zCached = agent_config_get("model");
  return zCached
    ? zCached
    : db_get("agent-model", db_get("agent-ollama-model", ""));
}

/*
** Return the configured chat provider, with legacy inference fallback.
*/
static const char *agent_chat_provider(void){
  static char *zCached = 0;
  char *zCmd = 0;
  fossil_free(zCached);
  zCached = agent_config_get("provider");
  if( zCached ) return zCached;
  zCached = db_get("agent-provider", 0);
  if( zCached ) return zCached;
  zCmd = agent_config_get("command");
  if( zCmd==0 ) zCmd = db_get("agent-command", "");
  zCached = mprintf("%s", agent_infer_provider(zCmd));
  fossil_free(zCmd);
  return zCached;
}

/*
** Return the configured embedding model, with fallback to the chat model.
*/
static const char *agent_embedding_model(void){
  static char *zCached = 0;
  fossil_free(zCached);
  zCached = agent_config_get("embedding_model");
  return zCached
    ? zCached
    : db_get("agent-embedding-model", agent_default_model());
}

/*
** Return the configured embedding provider, with legacy inference fallback.
*/
static const char *agent_embedding_provider(void){
  static char *zCached = 0;
  char *zCmd = 0;
  fossil_free(zCached);
  zCached = agent_config_get("embedding_provider");
  if( zCached ) return zCached;
  zCached = db_get("agent-embedding-provider", 0);
  if( zCached ) return zCached;
  zCmd = agent_config_get("embedding_command");
  if( zCmd==0 ) zCmd = db_get("agent-embedding-command", "");
  if( zCmd[0] ){
    zCached = mprintf("%s", agent_infer_provider(zCmd));
  }else{
    zCached = mprintf("%s", agent_chat_provider());
  }
  fossil_free(zCmd);
  return zCached;
}

/*
** Return the configured chat command template, with legacy fallback.
*/
static const char *agent_command_template(void){
  static char *zCached = 0;
  fossil_free(zCached);
  zCached = agent_config_get("command");
  return zCached ? zCached : db_get("agent-command", "");
}

/*
** Return the configured embedding command template, if any.
*/
static const char *agent_embedding_template(void){
  static char *zCached = 0;
  fossil_free(zCached);
  zCached = agent_config_get("embedding_command");
  return zCached ? zCached : db_get("agent-embedding-command", "");
}

/*
** Return non-zero if the current embedding configuration is usable for
** embedding generation.
*/
static int agent_embedding_is_available(void){
  const char *zProvider = agent_embedding_provider();
  const char *zModel = agent_embedding_model();
  const char *zCmd = agent_embedding_template();
  if( zModel==0 || zModel[0]==0 ) return 0;
  if( zCmd && zCmd[0] ) return 1;
  return agent_provider_string_property(zProvider, "builtin_embedding_fallback")
      != 0;
}

static int agent_provider_is_known(const char *zProvider){
  if( zProvider==0 || zProvider[0]==0 ) return 0;
#ifdef FOSSIL_ENABLE_JSON
  cson_value *pRoot = 0;
  Blob json = BLOB_INITIALIZER;
  cson_object *pRootObj = 0;
  int rc = 0;
  pRootObj = agent_metadata_parse_object(&pRoot, &json);
  if( agent_provider_array_count(pRootObj)>0 ){
    rc = agent_provider_object(pRootObj, zProvider)!=0;
  }else{
    rc = 1;
  }
  cson_value_free(pRoot);
  blob_reset(&json);
  return rc;
#else
  return 1;
#endif
}

/*
** Return non-zero if zProvider may use "auto" to defer model selection to
** an external CLI.
*/
static int agent_provider_accepts_auto(const char *zProvider){
  return agent_provider_bool_property(zProvider, "accepts_auto", 0);
}

/*
** Return non-zero if zProvider should reject obvious Ollama-style model names
** before backend launch.
*/
static int agent_provider_rejects_ollama_models(const char *zProvider){
  return agent_provider_bool_property(zProvider, "rejects_ollama_models", 0);
}

/*
** Return non-zero if the current chat provider should remain fixed in the UI.
** This stays locked until provider switching has a complete server-side
** implementation.
*/
static int agent_chat_provider_locked(void){
  int rc = 1;
#ifdef FOSSIL_ENABLE_JSON
  cson_value *pRoot = 0;
  Blob json = BLOB_INITIALIZER;
  cson_object *pRootObj = 0;
  pRootObj = agent_metadata_parse_object(&pRoot, &json);
  rc = agent_json_bool(pRootObj, "chat_provider_locked", 1);
  cson_value_free(pRoot);
  blob_reset(&json);
#endif
  return rc;
}

/*
** Emit the configured provider choice names. Falls back to the current
** provider when no explicit catalog is present.
*/
static void agent_emit_provider_choices_json(const char *zCurrentProvider){
#ifdef FOSSIL_ENABLE_JSON
  cson_value *pRoot = 0;
  Blob json = BLOB_INITIALIZER;
  cson_object *pRootObj = agent_metadata_parse_object(&pRoot, &json);
  cson_array *pProviders = agent_provider_array(pRootObj);
  unsigned int i, n = pProviders ? cson_array_length_get(pProviders) : 0;
  if( n>0 ){
    fossil_print("[");
    for(i=0; i<n; i++){
      cson_value *pVal = cson_array_get(pProviders, i);
      cson_object *pProvider = cson_value_is_object(pVal)
        ? cson_value_get_object(pVal) : 0;
      const char *zName = agent_json_cstr(pProvider, "name");
      if( zName && zName[0] ){
        CX("%s%!j", i ? "," : "", zName);
      }
    }
    fossil_print("]");
    cson_value_free(pRoot);
    blob_reset(&json);
    return;
  }
  cson_value_free(pRoot);
  blob_reset(&json);
#endif
  fossil_print("[");
  if( zCurrentProvider && zCurrentProvider[0] ){
    CX("%!j", zCurrentProvider);
  }
  fossil_print("]");
}

/*
** Emit the configured model suggestions for zProvider. The current model is
** emitted separately by agent_emit_config_json(), so these are only hints.
*/
static void agent_emit_model_suggestions_json(const char *zProvider){
#ifdef FOSSIL_ENABLE_JSON
  cson_value *pRoot = 0;
  Blob json = BLOB_INITIALIZER;
  cson_object *pRootObj = agent_metadata_parse_object(&pRoot, &json);
  cson_object *pProvider = agent_provider_object(pRootObj, zProvider);
  cson_value *pVal = pProvider ? cson_object_get(pProvider, "model_suggestions") : 0;
  cson_array *pModels = (pVal && cson_value_is_array(pVal))
    ? cson_value_get_array(pVal) : 0;
  unsigned int i, n = pModels ? cson_array_length_get(pModels) : 0;
  fossil_print("[");
  for(i=0; i<n; i++){
    cson_value *pModel = cson_array_get(pModels, i);
    const char *zName = pModel ? cson_value_get_cstr(pModel) : 0;
    if( zName && zName[0] ){
      CX("%s%!j", i ? "," : "", zName);
    }
  }
  fossil_print("]");
  cson_value_free(pRoot);
  blob_reset(&json);
#else
  (void)zProvider;
  fossil_print("[]");
#endif
}

/*
** Extract the executable token from a command template, preserving path
** components but dropping surrounding quotes.
*/
static char *agent_command_executable(const char *zCmdTmpl){
  Blob out = BLOB_INITIALIZER;
  char chQuote = 0;
  const char *z = zCmdTmpl;
  if( z==0 ) return 0;
  while( fossil_isspace(z[0]) ) z++;
  while( z[0] && (!fossil_isspace(z[0]) || chQuote) ){
    if( z[0]=='"' || z[0]=='\'' ){
      if( chQuote==0 ){
        chQuote = z[0];
      }else if( chQuote==z[0] ){
        chQuote = 0;
      }else{
        blob_append(&out, z, 1);
      }
    }else{
      blob_append(&out, z, 1);
    }
    z++;
  }
  if( blob_size(&out)==0 ){
    blob_reset(&out);
    return 0;
  }
  return blob_str(&out);
}

/*
** Resolve zCmdTmpl to a runnable executable path if possible.
**
** On success, returns non-zero and writes a newly allocated resolved path to
** *pzResolved if non-NULL. On failure, returns 0 and writes a newly allocated
** explanation to *pzDetail if non-NULL.
*/
static int agent_command_is_ready(
  const char *zCmdTmpl,
  char **pzResolved,
  char **pzDetail
){
  char *zExec = 0;
  char *zFull = 0;
  int rc = 0;
  if( pzResolved ) *pzResolved = 0;
  if( pzDetail ) *pzDetail = 0;
  if( zCmdTmpl==0 || zCmdTmpl[0]==0 ){
    if( pzDetail ) *pzDetail = mprintf("missing command template");
    return 0;
  }
  zExec = agent_command_executable(zCmdTmpl);
  if( zExec==0 || zExec[0]==0 ){
    if( pzDetail ) *pzDetail = mprintf("unable to determine executable");
    fossil_free(zExec);
    return 0;
  }
  if( file_is_absolute_path(zExec) || strchr(zExec, '/')!=0
   || strchr(zExec, '\\')!=0
  ){
    zFull = mprintf("%s", zExec);
  }else{
    zFull = file_fullexename(zExec);
  }
  if( zFull && zFull[0] && file_isexe(zFull, ExtFILE) ){
    if( pzResolved ){
      *pzResolved = mprintf("%s", zFull);
    }
    if( pzDetail ) *pzDetail = mprintf("ok");
    rc = 1;
  }else{
    if( pzResolved && zFull && zFull[0] ){
      *pzResolved = mprintf("%s", zFull);
    }
    if( pzDetail ) *pzDetail = mprintf("executable not found: %s", zExec);
    rc = 0;
  }
  fossil_free(zExec);
  fossil_free(zFull);
  return rc;
}

/*
** Validate a provider/model pair and return a newly allocated explanation in
** *pzErr on failure. Returns non-zero if the pair is valid.
*/
static int agent_validate_provider_model_ex(
  const char *zProvider,
  const char *zModel,
  char **pzErr
){
  Blob err = BLOB_INITIALIZER;
  int rc;
  if( pzErr ) *pzErr = 0;
  rc = agent_validate_provider_model(zProvider, zModel, &err)==0;
  if( !rc && pzErr ){
    *pzErr = mprintf("%s", blob_str(&err));
  }
  blob_reset(&err);
  return rc;
}

/*
** Return non-zero if the embedding backend is operationally configured, and
** optionally return the resolved executable path and status detail.
*/
static int agent_embedding_backend_ready(
  const char *zProvider,
  const char *zModel,
  const char *zCmdTmpl,
  char **pzResolved,
  char **pzDetail
){
  const char *zFallback = 0;
  if( pzResolved ) *pzResolved = 0;
  if( pzDetail ) *pzDetail = 0;
  if( zModel==0 || zModel[0]==0 ){
    if( pzDetail ) *pzDetail = mprintf("missing embedding model");
    return 0;
  }
  if( zCmdTmpl && zCmdTmpl[0] ){
    return agent_command_is_ready(zCmdTmpl, pzResolved, pzDetail);
  }
  zFallback = agent_provider_string_property(zProvider, "builtin_embedding_fallback");
  if( zFallback && zFallback[0] ){
    int rc = agent_command_is_ready(zFallback, pzResolved, pzDetail);
    if( rc && pzDetail ){
      fossil_free(*pzDetail);
      *pzDetail = mprintf("builtin %s fallback via %s", zProvider, zFallback);
    }
    return rc;
  }
  if( pzDetail ){
    *pzDetail = mprintf("no embedding command configured");
  }
  return 0;
}

/*
** Built-in phase registry for structured orchestration contracts.
*/
static const AgentPhase aAgentPhaseBuiltin[] = {
  {"init", "Initialize", "Resolve effective configuration and establish run context."},
  {"explore", "Explore", "Inspect repository state and gather relevant context."},
  {"spec", "Specify", "Convert gathered context into a concrete task specification."},
  {"design", "Design", "Develop an implementation approach and decision record."},
  {"tasks", "Tasks", "Break the plan into bounded executable tasks."},
  {"apply", "Apply", "Execute approved changes against repository state."},
  {"verify", "Verify", "Check results, run validations, and summarize remaining risks."},
  {"archive", "Archive", "Persist final artifacts and run summaries."},
  {"selftest", "Self-Test", "Run agent-focused health or confidence checks."},
  {"diagnostics", "Diagnostics", "Capture inspectable debugging and environment output."}
};

/*
** Return the number of built-in orchestration phases.
*/
static int agent_phase_registry_count(void){
  return (int)count(aAgentPhaseBuiltin);
}

/*
** Locate a built-in phase definition by name.
*/
static const AgentPhase *agent_phase_find(const char *zName){
  unsigned int i;
  for(i=0; i<count(aAgentPhaseBuiltin); i++){
    if( fossil_strcmp(aAgentPhaseBuiltin[i].zName, zName)==0 ){
      return &aAgentPhaseBuiltin[i];
    }
  }
  return 0;
}

/*
** Emit a JSON array of phase names from a comma-separated list.
*/
static void agent_emit_phase_name_array(const char *zList){
  const char *z = zList ? zList : "";
  int first = 1;
  fossil_print("[");
  while( z[0] ){
    Blob tok = BLOB_INITIALIZER;
    while( fossil_isspace(z[0]) || z[0]==',' ) z++;
    while( z[0] && z[0]!=',' ){
      blob_append(&tok, z, 1);
      z++;
    }
    blob_trim(&tok);
    if( blob_size(&tok)>0 ){
      CX("%s%!j", first ? "" : ",", blob_str(&tok));
      first = 0;
    }
    blob_reset(&tok);
  }
  CX("]");
}

/*
** Append a JSON array of phase names from a comma-separated list.
*/
static void agent_append_phase_name_array(Blob *pOut, const char *zList){
  const char *z = zList ? zList : "";
  int first = 1;
  blob_append(pOut, "[", 1);
  while( z[0] ){
    Blob tok = BLOB_INITIALIZER;
    while( fossil_isspace(z[0]) || z[0]==',' ) z++;
    while( z[0] && z[0]!=',' ){
      blob_append(&tok, z, 1);
      z++;
    }
    blob_trim(&tok);
    if( blob_size(&tok)>0 ){
      blob_appendf(pOut, "%s%!j", first ? "" : ",", blob_str(&tok));
      first = 0;
    }
    blob_reset(&tok);
  }
  blob_append(pOut, "]", 1);
}

/*
** Emit the full built-in phase registry as a JSON array.
*/
static void agent_emit_phase_registry_json(void){
  unsigned int i;
  fossil_print("[");
  for(i=0; i<count(aAgentPhaseBuiltin); i++){
    const AgentPhase *p = &aAgentPhaseBuiltin[i];
    CX("%s{\"name\":%!j,\"title\":%!j,\"description\":%!j}",
       i ? "," : "",
       p->zName, p->zTitle, p->zDescription);
  }
  CX("]");
}

/*
** Emit a JSON object describing the effective chat and embedding config for
** sidCurrent. If sidCurrent refers to an existing session, chat provider/model
** reflect that session rather than the current default.
*/
static void agent_emit_config_json(int sidCurrent){
  char *zChatProvider = mprintf("%s", agent_chat_session_provider(
    sidCurrent, agent_chat_provider()
  ));
  char *zChatModel = mprintf("%s", agent_chat_session_model(
    sidCurrent, agent_default_model()
  ));
  char *zEmbedProvider = mprintf("%s", agent_embedding_provider());
  char *zEmbedModel = mprintf("%s", agent_embedding_model());
  char *zCmd = mprintf("%s", agent_command_template());
  char *zEmbedCmd = mprintf("%s", agent_embedding_template());
  char *zSource = agent_config_source();
  int chatProviderLocked = agent_chat_provider_locked();
  int chatSupportsStreaming = 0;
  int chatSupportsModelDiscovery = 0;
  int embeddingAvailable = agent_embedding_is_available();
  int embeddingSupportsModelDiscovery = 0;
  int chatProviderKnown = agent_provider_is_known(zChatProvider);
  int embeddingProviderKnown = agent_provider_is_known(zEmbedProvider);
  int chatModelValid = 0;
  int chatCommandReady = 0;
  int chatOk = 0;
  int embeddingModelValid = 0;
  int embeddingCommandReady = 0;
  int embeddingOk = 0;
  int overallOk = 0;
  int nPhase = agent_phase_registry_count();
  int nProviderChoice = 0;
  int nModelChoice = 0;
  int nCapability = agent_capability_registry_count();
  char *zChatModelError = 0;
  char *zChatCommandPath = 0;
  char *zChatCommandDetail = 0;
  char *zEmbeddingModelError = 0;
  char *zEmbeddingCommandPath = 0;
  char *zEmbeddingCommandDetail = 0;
  (void)nProviderChoice;
  (void)nModelChoice;
  chatSupportsStreaming = agent_provider_bool_property(
    zChatProvider, "supports_streaming", 0
  );
  chatSupportsModelDiscovery = agent_provider_bool_property(
    zChatProvider, "supports_model_discovery", 0
  );
  embeddingSupportsModelDiscovery = agent_provider_bool_property(
    zEmbedProvider, "supports_model_discovery", 0
  );
  chatModelValid = agent_validate_provider_model_ex(
    zChatProvider, zChatModel, &zChatModelError
  );
  chatCommandReady = agent_command_is_ready(
    zCmd, &zChatCommandPath, &zChatCommandDetail
  );
  chatOk = chatProviderKnown && chatModelValid && chatCommandReady;
  embeddingModelValid = agent_validate_provider_model_ex(
    zEmbedProvider, zEmbedModel, &zEmbeddingModelError
  );
  embeddingCommandReady = agent_embedding_backend_ready(
    zEmbedProvider, zEmbedModel, zEmbedCmd,
    &zEmbeddingCommandPath, &zEmbeddingCommandDetail
  );
  embeddingOk = embeddingProviderKnown && embeddingModelValid
             && embeddingAvailable && embeddingCommandReady;
  overallOk = chatOk && embeddingOk;
  CX("{\"sid\":%d,\"source\":%!j,\"chat_provider\":%!j,\"chat_command\":%!j,"
     "\"chat_model\":%!j,\"embedding_provider\":%!j,"
     "\"embedding_command\":%!j,\"embedding_model\":%!j,"
     "\"chat_provider_locked\":%d,\"chat_supports_streaming\":%d,"
     "\"chat_supports_model_discovery\":%d,\"embedding_available\":%d,"
     "\"embedding_supports_model_discovery\":%d,"
     "\"chat_provider_known\":%d,\"embedding_provider_known\":%d,",
     sidCurrent, zSource, zChatProvider, zCmd, zChatModel,
     zEmbedProvider, zEmbedCmd, zEmbedModel,
     chatProviderLocked, chatSupportsStreaming, chatSupportsModelDiscovery,
     embeddingAvailable, embeddingSupportsModelDiscovery,
     chatProviderKnown, embeddingProviderKnown);
  CX("\"chat_provider_choices\":");
  agent_emit_provider_choices_json(zChatProvider);
  CX(",");
  CX("\"chat_model_suggestions\":");
  agent_emit_model_suggestions_json(zChatProvider);
  CX(",\"phase_count\":%d,\"phases\":", nPhase);
  agent_emit_phase_registry_json();
  CX(",\"capability_count\":%d,\"capabilities\":", nCapability);
  agent_emit_capability_registry_json();
  CX(",\"verification\":{"
     "\"chat_ok\":%d,"
     "\"chat_model_valid\":%d,"
     "\"chat_model_error\":%!j,"
     "\"chat_command_ready\":%d,"
     "\"chat_command_path\":%!j,"
     "\"chat_command_detail\":%!j,"
     "\"embedding_ok\":%d,"
     "\"embedding_model_valid\":%d,"
     "\"embedding_model_error\":%!j,"
     "\"embedding_command_ready\":%d,"
     "\"embedding_command_path\":%!j,"
     "\"embedding_command_detail\":%!j,"
     "\"overall_ok\":%d}",
     chatOk, chatModelValid, zChatModelError ? zChatModelError : "",
     chatCommandReady, zChatCommandPath ? zChatCommandPath : "",
     zChatCommandDetail ? zChatCommandDetail : "",
     embeddingOk, embeddingModelValid,
     zEmbeddingModelError ? zEmbeddingModelError : "",
     embeddingCommandReady,
     zEmbeddingCommandPath ? zEmbeddingCommandPath : "",
     zEmbeddingCommandDetail ? zEmbeddingCommandDetail : "",
     overallOk);
  CX("}\n");
  fossil_free(zChatModelError);
  fossil_free(zChatCommandPath);
  fossil_free(zChatCommandDetail);
  fossil_free(zEmbeddingModelError);
  fossil_free(zEmbeddingCommandPath);
  fossil_free(zEmbeddingCommandDetail);
  fossil_free(zSource);
  fossil_free(zChatProvider);
  fossil_free(zChatModel);
  fossil_free(zEmbedProvider);
  fossil_free(zEmbedModel);
  fossil_free(zCmd);
  fossil_free(zEmbedCmd);
}

/*
** Wrap zCmd in a stable shell invocation with exported agent env vars.
*/
static void agent_prepare_command(
  Blob *pOut,
  const char *zMode,
  const char *zProvider,
  const char *zModel,
  Blob *pCmd
){
  Blob model = BLOB_INITIALIZER;
  Blob mode = BLOB_INITIALIZER;
  Blob provider = BLOB_INITIALIZER;
  Blob cmd = BLOB_INITIALIZER;
  blob_append_escaped_arg(&model, zModel ? zModel : "", 0);
  blob_append_escaped_arg(&mode, zMode ? zMode : "", 0);
  blob_append_escaped_arg(&provider, zProvider ? zProvider : "", 0);
  blob_append_escaped_arg(&cmd, blob_str(pCmd), 0);
  blob_zero(pOut);
  blob_appendf(
    pOut,
    "env FOSSIL_AGENT_MODEL=%s FOSSIL_AGENT_MODE=%s FOSSIL_AGENT_PROVIDER=%s"
    " sh -lc %s 2>&1",
    blob_str(&model), blob_str(&mode), blob_str(&provider), blob_str(&cmd)
  );
  blob_reset(&model);
  blob_reset(&mode);
  blob_reset(&provider);
  blob_reset(&cmd);
}

/*
** Validate a provider/model pair before invoking the backend.
*/
static int agent_validate_provider_model(
  const char *zProvider,
  const char *zModel,
  Blob *pErr
){
  if( zModel==0 || zModel[0]==0 ){
    blob_appendf(pErr, "missing model parameter");
    return 1;
  }
  if( zProvider==0 || zProvider[0]==0 ) return 0;
  if( fossil_stricmp(zModel, "auto")==0 && !agent_provider_accepts_auto(zProvider) ){
    blob_appendf(pErr,
      "model \"auto\" is not valid for provider %s", zProvider
    );
    return 1;
  }
  if( agent_provider_accepts_auto(zProvider) ){
    if( agent_provider_rejects_ollama_models(zProvider)
     && agent_model_looks_ollama(zModel)
    ){
      blob_appendf(pErr,
        "model \"%s\" looks like an Ollama model but provider is %s",
        zModel, zProvider
      );
      return 1;
    }
  }
  return 0;
}

/*
** Built-in capability registry for agent-accessible operations.
*/
static const AgentCapability aAgentCapabilityBuiltin[] = {
  {
    "agent_context",
    "builtin",
    "Assemble repository context, including file map, pending changes, and retrieval context.",
    0, 0, 0
  },
  {
    "agent_run",
    "builtin",
    "Invoke the configured chat backend with the selected provider and model.",
    0, 1, 0
  }
};

static int agent_capability_registry_count(void){
  return count(aAgentCapabilityBuiltin);
}

/*
** Locate a built-in capability definition by name.
*/
static const AgentCapability *agent_capability_find(const char *zName){
  unsigned int i;
  for(i=0; i<count(aAgentCapabilityBuiltin); i++){
    if( fossil_strcmp(aAgentCapabilityBuiltin[i].zName, zName)==0 ){
      return &aAgentCapabilityBuiltin[i];
    }
  }
  return 0;
}

/*
** Count comma-separated tokens in zList.
*/
static int agent_list_count(const char *zList){
  int n = 0;
  int inToken = 0;
  const char *z = zList ? zList : "";
  while( z[0] ){
    if( z[0]==',' ){
      inToken = 0;
    }else if( !fossil_isspace(z[0]) && !inToken ){
      inToken = 1;
      n++;
    }
    z++;
  }
  return n;
}

/*
** Validate that all phase names in zList are declared. Return non-zero if
** valid, otherwise append a human-readable error to pErr and return 0.
*/
static int agent_recipe_phases_valid(const char *zList, Blob *pErr){
  const char *z = zList ? zList : "";
  while( z[0] ){
    Blob tok = BLOB_INITIALIZER;
    while( fossil_isspace(z[0]) || z[0]==',' ) z++;
    while( z[0] && z[0]!=',' ){
      blob_append(&tok, z, 1);
      z++;
    }
    blob_trim(&tok);
    if( blob_size(&tok)>0 && agent_phase_find(blob_str(&tok))==0 ){
      blob_appendf(pErr, "unknown phase: %s", blob_str(&tok));
      blob_reset(&tok);
      return 0;
    }
    blob_reset(&tok);
  }
  return 1;
}

/*
** Emit a JSON array of capability names from a comma-separated list.
*/
static void agent_emit_capability_name_array(const char *zList){
  const char *z = zList ? zList : "";
  int first = 1;
  fossil_print("[");
  while( z[0] ){
    Blob tok = BLOB_INITIALIZER;
    while( fossil_isspace(z[0]) || z[0]==',' ) z++;
    while( z[0] && z[0]!=',' ){
      blob_append(&tok, z, 1);
      z++;
    }
    blob_trim(&tok);
    if( blob_size(&tok)>0 ){
      CX("%s%!j", first ? "" : ",", blob_str(&tok));
      first = 0;
    }
    blob_reset(&tok);
  }
  CX("]");
}

/*
** Append a JSON array of capability names from a comma-separated list.
*/
static void agent_append_capability_name_array(Blob *pOut, const char *zList){
  const char *z = zList ? zList : "";
  int first = 1;
  blob_append(pOut, "[", 1);
  while( z[0] ){
    Blob tok = BLOB_INITIALIZER;
    while( fossil_isspace(z[0]) || z[0]==',' ) z++;
    while( z[0] && z[0]!=',' ){
      blob_append(&tok, z, 1);
      z++;
    }
    blob_trim(&tok);
    if( blob_size(&tok)>0 ){
      blob_appendf(pOut, "%s%!j", first ? "" : ",", blob_str(&tok));
      first = 0;
    }
    blob_reset(&tok);
  }
  blob_append(pOut, "]", 1);
}

/*
** Emit a JSON array describing all built-in capabilities.
*/
static void agent_emit_capability_registry_json(void){
  unsigned int i;
  fossil_print("[");
  for(i=0; i<count(aAgentCapabilityBuiltin); i++){
    const AgentCapability *p = &aAgentCapabilityBuiltin[i];
    CX("%s{\"name\":%!j,\"kind\":%!j,\"description\":%!j,"
       "\"requires_write\":%d,\"requires_network\":%d,"
       "\"requires_confirmation\":%d}",
       i ? "," : "",
       p->zName, p->zKind, p->zDescription,
       p->requiresWrite, p->requiresNetwork, p->requiresConfirm);
  }
  CX("]");
}

/*
** Validate that all capability names in zList are declared. Return non-zero if
** valid, otherwise append a human-readable error to pErr and return 0.
*/
static int agent_recipe_capabilities_valid(const char *zList, Blob *pErr){
  const char *z = zList ? zList : "";
  while( z[0] ){
    Blob tok = BLOB_INITIALIZER;
    while( fossil_isspace(z[0]) || z[0]==',' ) z++;
    while( z[0] && z[0]!=',' ){
      blob_append(&tok, z, 1);
      z++;
    }
    blob_trim(&tok);
    if( blob_size(&tok)>0 && agent_capability_find(blob_str(&tok))==0 ){
      blob_appendf(pErr, "unknown capability: %s", blob_str(&tok));
      blob_reset(&tok);
      return 0;
    }
    blob_reset(&tok);
  }
  return 1;
}

/*
** Built-in recipe definitions for repeatable orchestration tasks.
*/
static const AgentRecipe aAgentRecipeBuiltin[] = {
  {
    "summarize-context",
    "Summarize Repository Context",
    "Assemble repository context and ask the configured backend for a concise summary.",
    "fossil agent recipe run summarize-context ?QUERY? [--json]",
    "doc/ai/guidance/summarize-context.md",
    "explore",
    "agent_context,agent_run",
    "set q $query\n"
    "if {[string compare $q \"\"] == 0} {set q \"repository summary\"}\n"
    "set context [agent_context $q $model]\n"
    "if {[string compare $context \"\"] == 0} {\n"
    "  return \"No repository context available.\"\n"
    "}\n"
    "if {[string compare $guidance \"\"] != 0} {\n"
    "  set prompt \"Repository guidance:\\n$guidance\\n\\nSummarize this repository context for a developer. Keep it concise and concrete.\\n\\n$context\"\n"
    "} else {\n"
    "  set prompt \"Summarize this repository context for a developer. Keep it concise and concrete.\\n\\n$context\"\n"
    "}\n"
    "return [agent_run $provider $model $prompt]\n"
  },
  {
    "review-change",
    "Review Pending Changes",
    "Assemble repository context and pending changes, then ask the backend for a review focused on risks and test gaps.",
    "fossil agent recipe run review-change [--json]",
    "doc/ai/guidance/review-change.md",
    "explore,verify",
    "agent_context,agent_run",
    "set context [agent_context \"pending changes review\" $model]\n"
    "if {[string compare $context \"\"] == 0} {\n"
    "  return \"No repository context available.\"\n"
    "}\n"
    "if {[string compare $guidance \"\"] != 0} {\n"
    "  set prompt \"Repository guidance:\\n$guidance\\n\\nReview the repository context and pending changes. Prioritize bugs, regressions, and missing tests.\\n\\n$context\"\n"
    "} else {\n"
    "  set prompt \"Review the repository context and pending changes. Prioritize bugs, regressions, and missing tests.\\n\\n$context\"\n"
    "}\n"
    "return [agent_run $provider $model $prompt]\n"
  }
};

/*
** Repository storage for persisted agent runs and diagnostics artifacts.
*/
static const char zAgentRunSchema[] =
@ CREATE TABLE repository.agentrun(
@   runid INTEGER PRIMARY KEY AUTOINCREMENT,
@   created_at JULIANDAY DEFAULT (julianday('now')),
@   kind TEXT NOT NULL,
@   name TEXT,
@   status TEXT,
@   summary TEXT,
@   payload TEXT
@ );
;

/*
** Ensure the repository table used to persist saved agent runs exists.
*/
static void agent_run_create_tables(void){
  if( !db_table_exists("repository","agentrun") ){
    db_multi_exec(zAgentRunSchema/*works-like:""*/);
  }
}

/*
** Persist a saved run payload and return its row id.
*/
static int agent_run_record(
  const char *zKind,
  const char *zName,
  const char *zStatus,
  const char *zSummary,
  const char *zPayload
){
  agent_run_create_tables();
  db_multi_exec(
    "INSERT INTO agentrun(created_at,kind,name,status,summary,payload)"
    " VALUES(julianday('now'),%Q,%Q,%Q,%Q,%Q)",
    zKind ? zKind : "",
    zName ? zName : "",
    zStatus ? zStatus : "",
    zSummary ? zSummary : "",
    zPayload ? zPayload : ""
  );
  return db_last_insert_rowid();
}

/*
** Locate a built-in recipe definition by name.
*/
static const AgentRecipe *agent_recipe_find(const char *zName){
  unsigned int i;
  for(i=0; i<count(aAgentRecipeBuiltin); i++){
    if( fossil_strcmp(aAgentRecipeBuiltin[i].zName, zName)==0 ){
      return &aAgentRecipeBuiltin[i];
    }
  }
  return 0;
}

/*
** Return the first declared phase for a recipe, or NULL if none are declared.
*/
static const AgentPhase *agent_recipe_primary_phase(const AgentRecipe *pRecipe){
  const char *z;
  char *zName = 0;
  Blob tok = BLOB_INITIALIZER;
  if( pRecipe==0 || pRecipe->zPhases==0 ) return 0;
  z = pRecipe->zPhases;
  while( fossil_isspace(z[0]) || z[0]==',' ) z++;
  while( z[0] && z[0]!=',' ){
    blob_append(&tok, z, 1);
    z++;
  }
  blob_trim(&tok);
  if( blob_size(&tok)==0 ){
    blob_reset(&tok);
    return 0;
  }
  zName = mprintf("%s", blob_str(&tok));
  blob_reset(&tok);
  {
    const AgentPhase *pPhase = agent_phase_find(zName);
    fossil_free(zName);
    return pPhase;
  }
}

/*
** Return a single string by joining g.argv[iFrom..g.argc) with spaces.
*/
static char *agent_join_args(int iFrom){
  Blob out = BLOB_INITIALIZER;
  int i;
  for(i=iFrom; i<g.argc; i++){
    blob_appendf(&out, "%s%s", i>iFrom ? " " : "", g.argv[i]);
  }
  return blob_str(&out);
}

/*
** Return non-zero if zRef is a relative checkout path suitable for loading as
** a repository guidance artifact.
*/
static int agent_guidance_ref_is_safe(const char *zRef){
  if( zRef==0 || zRef[0]==0 ) return 0;
  if( zRef[0]=='/' || zRef[0]=='\\' ) return 0;
  if( strstr(zRef, "..")!=0 ) return 0;
  return 1;
}

/*
** Load all guidance refs from zList, appending concatenated text to pText and
** a JSON array of resolved artifact metadata to pJson. Return non-zero on
** success; otherwise append a human-readable error to pErr and return 0.
*/
static int agent_guidance_load(
  const char *zList,
  Blob *pText,
  Blob *pJson,
  Blob *pErr
){
  const char *z = zList ? zList : "";
  int first = 1;
  if( pJson ) blob_append(pJson, "[", 1);
  while( z[0] ){
    Blob tok = BLOB_INITIALIZER;
    Blob content = BLOB_INITIALIZER;
    char *zPath = 0;
    while( fossil_isspace(z[0]) || z[0]==',' ) z++;
    while( z[0] && z[0]!=',' ){
      blob_append(&tok, z, 1);
      z++;
    }
    blob_trim(&tok);
    if( blob_size(&tok)==0 ){
      blob_reset(&tok);
      continue;
    }
    if( !agent_guidance_ref_is_safe(blob_str(&tok)) ){
      blob_appendf(pErr, "unsafe guidance ref: %s", blob_str(&tok));
      blob_reset(&tok);
      if( pJson ) blob_append(pJson, "]", 1);
      return 0;
    }
    if( g.zLocalRoot==0 || g.zLocalRoot[0]==0 ){
      blob_appendf(pErr, "guidance requires a checkout-local root: %s",
                   blob_str(&tok));
      blob_reset(&tok);
      if( pJson ) blob_append(pJson, "]", 1);
      return 0;
    }
    zPath = mprintf("%s%s", g.zLocalRoot, blob_str(&tok));
    if( file_size(zPath, ExtFILE)<0 ){
      blob_appendf(pErr, "missing guidance artifact: %s", blob_str(&tok));
      fossil_free(zPath);
      blob_reset(&tok);
      if( pJson ) blob_append(pJson, "]", 1);
      return 0;
    }
    blob_read_from_file(&content, zPath, ExtFILE);
    if( blob_size(pText)>0 ) blob_append(pText, "\n\n", 2);
    blob_appendf(pText, "[%s]\n%s", blob_str(&tok), blob_str(&content));
    if( pJson ){
      blob_appendf(pJson, "%s{\"ref\":%!j,\"bytes\":%d}",
                   first ? "" : ",", blob_str(&tok), blob_size(&content));
      first = 0;
    }
    fossil_free(zPath);
    blob_reset(&content);
    blob_reset(&tok);
  }
  if( pJson ) blob_append(pJson, "]", 1);
  return 1;
}

/*
** Repository storage for agent chat messages.
*/
static const char zAgentChatSchema[] =
@ CREATE TABLE repository.agentchat_session(
@   sid INTEGER PRIMARY KEY AUTOINCREMENT,
@   ctime JULIANDAY DEFAULT (julianday('now')),
@   mtime JULIANDAY DEFAULT (julianday('now')),
@   xfrom TEXT,
@   provider TEXT,
@   model TEXT,
@   title TEXT
@ );
@ CREATE TABLE repository.agentchat(
@   acid INTEGER PRIMARY KEY AUTOINCREMENT,
@   sid INTEGER REFERENCES agentchat_session,
@   mtime JULIANDAY DEFAULT (julianday('now')),
@   xfrom TEXT,
@   role TEXT NOT NULL,
@   kind TEXT,
@   provider TEXT,
@   model TEXT,
@   meta TEXT,
@   msg TEXT NOT NULL
@ );
;

/*
** Ensure the repository table used by /agentui exists.
*/
static void agent_chat_create_tables(void){
  if( !db_table_exists("repository","agentchat") ){
    db_multi_exec(zAgentChatSchema/*works-like:""*/);
  }else{
    if( !db_table_exists("repository","agentchat_session") ){
      db_multi_exec(
        "CREATE TABLE repository.agentchat_session("
        "  sid INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ctime JULIANDAY DEFAULT (julianday('now')),"
        "  mtime JULIANDAY DEFAULT (julianday('now')),"
        "  xfrom TEXT,"
        "  provider TEXT,"
        "  model TEXT,"
        "  title TEXT"
        ");"
      );
    }
    if( !db_table_has_column("repository","agentchat","sid") ){
      db_multi_exec("ALTER TABLE agentchat ADD COLUMN sid INTEGER");
    }
    if( !db_table_has_column("repository","agentchat","kind") ){
      db_multi_exec("ALTER TABLE agentchat ADD COLUMN kind TEXT");
    }
    if( !db_table_has_column("repository","agentchat","meta") ){
      db_multi_exec("ALTER TABLE agentchat ADD COLUMN meta TEXT");
    }
    if( !db_table_has_column("repository","agentchat_session","provider") ){
      db_multi_exec("ALTER TABLE agentchat_session ADD COLUMN provider TEXT");
    }
    if( !db_table_has_column("repository","agentchat_session","model") ){
      db_multi_exec("ALTER TABLE agentchat_session ADD COLUMN model TEXT");
    }
    if( !db_table_has_column("repository","agentchat","provider") ){
      db_multi_exec("ALTER TABLE agentchat ADD COLUMN provider TEXT");
    }
  }
}

/*
** Create and return a new chat session id.
*/
static int agent_chat_session_create(
  const char *zUser,
  const char *zProvider,
  const char *zModel
){
  agent_chat_create_tables();
  db_multi_exec(
    "INSERT INTO agentchat_session(ctime,mtime,xfrom,provider,model,title)"
    " VALUES(julianday('now'),julianday('now'),%Q,%Q,%Q,'New Chat')",
    zUser ? zUser : "",
    zProvider ? zProvider : "",
    zModel ? zModel : ""
  );
  return db_last_insert_rowid();
}

/*
** Return non-zero if sid exists.
*/
static int agent_chat_session_exists(int sid){
  return sid>0 && db_exists("SELECT 1 FROM agentchat_session WHERE sid=%d", sid);
}

static int agent_chat_latest_session_user(const char *zUser){
  return db_int(0, "SELECT max(sid) FROM repository.ai_chat_session"
                   " WHERE user=%Q", zUser);
}

static int agent_chat_current_session_user(const char *zUser){
  int cur = db_int(0, "SELECT current_sid FROM repository.ai_chat_user"
                      " WHERE user=%Q", zUser);
  return cur ? cur : agent_chat_latest_session_user(zUser);
}

static int agent_chat_current_session(const char *zUser){
  int sid;
  agent_chat_create_tables();
  sid = atoi(PD("sid","0"));
  if( sid>0 && agent_chat_session_exists(sid) ){
    return sid;
  }
  if( PB("new") ){
    return agent_chat_session_create(zUser, agent_chat_provider(), agent_default_model());
  }
  sid = db_int(0,
    "SELECT sid FROM agentchat_session"
    " WHERE xfrom=%Q OR (%Q='' AND xfrom='')"
    " ORDER BY mtime DESC, sid DESC LIMIT 1",
    zUser ? zUser : "", zUser ? zUser : ""
  );
  return sid>0 ? sid : agent_chat_session_create(zUser, agent_chat_provider(), agent_default_model());
}

static int agent_chat_latest_session(const char *zUser){
  if( !db_table_exists("repository","agentchat_session") ) return 0;
  return db_int(0,
    "SELECT sid FROM agentchat_session"
    " WHERE xfrom=%Q OR (%Q='' AND xfrom='')"
    " ORDER BY mtime DESC, sid DESC LIMIT 1",
    zUser ? zUser : "", zUser ? zUser : ""
  );
}

/*
** Update session metadata after a new message.
*/
static void agent_chat_session_touch(
  int sid,
  const char *zMsg,
  const char *zProvider,
  const char *zModel
){
  Blob title = BLOB_INITIALIZER;
  int n;
  if( sid<=0 ) return;
  if( zMsg==0 ) zMsg = "";
  while( fossil_isspace(zMsg[0]) ) zMsg++;
  n = (int)strlen(zMsg);
  if( n>60 ) n = 60;
  blob_append(&title, zMsg, n);
  blob_trim(&title);
  db_multi_exec(
    "UPDATE agentchat_session"
    " SET mtime=julianday('now'),"
    " provider=coalesce(nullif(%Q,''),provider),"
    " model=coalesce(nullif(%Q,''),model),"
    " title=CASE"
    "   WHEN title IS NULL OR title='' OR title='New Chat'"
    "   THEN %Q ELSE title END"
    " WHERE sid=%d",
    zProvider ? zProvider : "",
    zModel ? zModel : "",
    blob_size(&title)>0 ? blob_str(&title) : "New Chat",
    sid
  );
  blob_reset(&title);
}

/*
** Persist a single agent chat message.
*/
static int agent_chat_save(
  int sid,
  const char *zUser,
  const char *zRole,
  const char *zKind,
  const char *zProvider,
  const char *zModel,
  const char *zMeta,
  const char *zMsg
){
  const char *zTitleMsg = zMsg;
  int acid;
  if( zMsg==0 || zMsg[0]==0 ) return 0;
  agent_chat_create_tables();
  db_multi_exec(
    "INSERT INTO agentchat(sid,mtime,xfrom,role,kind,provider,model,meta,msg)"
    " VALUES(%d,julianday('now'),%Q,%Q,%Q,%Q,%Q,%Q,%Q)",
    sid,
    zUser ? zUser : "",
    zRole ? zRole : "agent",
    zKind ? zKind : "message",
    zProvider ? zProvider : "",
    zModel ? zModel : "",
    zMeta ? zMeta : "",
    zMsg
  );
  acid = db_last_insert_rowid();
  if( zRole && fossil_strcmp(zRole,"system")==0 ){
    zTitleMsg = "";
  }
  agent_chat_session_touch(sid, zTitleMsg, zProvider, zModel);
  return acid;
}

/*
** Persist a system event for a chat session.
*/
static void agent_chat_save_event(
  int sid,
  const char *zUser,
  const char *zKind,
  const char *zProvider,
  const char *zModel,
  const char *zMeta,
  const char *zMsg
){
  (void)agent_chat_save(
    sid, zUser, "system", zKind, zProvider, zModel, zMeta, zMsg
  );
}

/*
** Return a compact state label for the latest stored event in sid.
*/
static const char *agent_chat_session_state(int sid){
  if( sid<=0 || !db_table_exists("repository","agentchat") ) return "";
  return db_text("",
    "SELECT CASE"
    "  WHEN role='agent' AND kind='reply' THEN 'reply'"
    "  WHEN role='agent' AND kind='error' THEN 'error'"
    "  WHEN role='system' AND kind='progress'"
    "   AND meta LIKE '%%\"status\":\"running\"%%' THEN 'running'"
    "  WHEN role='system' AND kind='progress'"
    "   AND meta LIKE '%%\"status\":\"ok\"%%' THEN 'ok'"
    "  WHEN role='system' AND kind='progress'"
    "   AND meta LIKE '%%\"status\":\"error\"%%' THEN 'error'"
    "  WHEN role='system' AND kind='tool' THEN 'tool'"
    "  WHEN role='system' AND kind='progress' THEN 'progress'"
    "  WHEN role='user' AND kind='prompt' THEN 'prompt'"
    "  ELSE coalesce(kind, role, '') END"
    " FROM agentchat WHERE sid=%d ORDER BY acid DESC LIMIT 1",
    sid
  );
}

/*
** Return the latest terminal agent event acid for sid, or 0 if none.
*/
static int agent_chat_latest_terminal_acid(int sid){
  if( sid<=0 || !db_table_exists("repository","agentchat") ) return 0;
  return db_int(0,
    "SELECT acid FROM agentchat"
    " WHERE sid=%d"
    "   AND role='agent'"
    "   AND kind IN ('reply','error')"
    " ORDER BY acid DESC LIMIT 1",
    sid
  );
}

/*
** True if acid is a terminal agent event for sid.
*/
static int agent_chat_is_terminal_acid(int sid, int acid){
  return sid>0 && acid>0 && db_exists(
    "SELECT 1 FROM agentchat"
    " WHERE sid=%d AND acid=%d"
    "   AND role='agent'"
    "   AND kind IN ('reply','error')",
    sid, acid
  );
}

/*
** Emit session list for the current user.
*/
static void agent_chat_render_sessions_to_blob(const char *zUser, int sidCurrent, Blob *pOut){
  Stmt q;
  int nLimit = db_get_int("agent-history-count", 50);
  if( nLimit<=0 ) return;
  if( !db_table_exists("repository","agentchat_session") ) return;
  db_prepare(&q,
    "SELECT sid, coalesce(nullif(title,''),'New Chat'),"
    "       coalesce(nullif(provider,''),'?'),"
    "       coalesce(nullif(model,''),'')"
    " FROM agentchat_session"
    " WHERE xfrom=%Q OR (%Q='' AND xfrom='')"
    " ORDER BY mtime DESC, sid DESC LIMIT %d",
    zUser ? zUser : "", zUser ? zUser : "", nLimit
  );
  while( db_step(&q)==SQLITE_ROW ){
    int sid = db_column_int(&q, 0);
    const char *zTitle = db_column_text(&q, 1);
    const char *zProvider = db_column_text(&q, 2);
    const char *zModel = db_column_text(&q, 3);
    const char *zState = agent_chat_session_state(sid);
    blob_appendf(pOut, "<div>\n");
    if( sid==sidCurrent ){
      blob_appendf(pOut, "<b>%h</b> <span class=\"dimmed\">[%h%s%h%s%h]</span>",
                   zTitle, zProvider, (zModel&&zModel[0]?" / ":""), zModel,
                   (zState&&zState[0]?" | ":""), zState);
    }else{
      blob_appendf(pOut, "<a href=\"%%R/agentui?sid=%d\">%h</a> <span class=\"dimmed\">[%h%s%h%s%h]</span>",
                   sid, zTitle, zProvider, (zModel&&zModel[0]?" / ":""), zModel,
                   (zState&&zState[0]?" | ":""), zState);
    }
    blob_appendf(pOut, "</div>\n");
  }
  db_finalize(&q);
}

static void agent_chat_render_sessions(const char *zUser, int sidCurrent){
  Blob out = BLOB_INITIALIZER;
  agent_chat_render_sessions_to_blob(zUser, sidCurrent, &out);
  CX("%s", blob_str(&out));
  blob_reset(&out);
}

/*
** Return non-zero if zMeta indicates that pool context was enabled.
*/
static int agent_chat_meta_context_enabled(const char *zMeta){
  return zMeta && strstr(zMeta, "\"context\":true")!=0;
}

/*
** Extract retrieval_qid from a small JSON-ish meta string. Returns 0 if absent.
*/
static int agent_chat_meta_retrieval_qid(const char *zMeta){
  const char *z;
  if( zMeta==0 ) return 0;
  z = strstr(zMeta, "\"retrieval_qid\":");
  if( z==0 ) return 0;
  z += 16;
  while( fossil_isspace(z[0]) ) z++;
  return atoi(z);
}

/*
** Emit recent saved agent chat messages for a session into the page log.
*/
static void agent_chat_render_history_to_blob(int sidCurrent, Blob *pOut){
  Stmt q;
  int nLimit = db_get_int("agent-history-count", 50);
  if( nLimit<=0 || sidCurrent<=0 ) return;
  if( !db_table_exists("repository","agentchat") ) return;
  if( db_table_exists("repository","ai_chat_eval") ){
    db_prepare(&q,
      "SELECT c.role, c.kind, c.provider, c.model, c.meta, c.msg,"
      "       coalesce(e.user_feedback,'')"
      "  FROM agentchat AS c"
      "  LEFT JOIN ai_chat_eval AS e ON e.sid=c.sid AND e.acid=c.acid"
      " WHERE c.sid=%d"
      " ORDER BY c.acid ASC LIMIT %d",
      sidCurrent, nLimit
    );
  }else{
    db_prepare(&q,
      "SELECT role, kind, provider, model, meta, msg, ''"
      " FROM agentchat WHERE sid=%d"
      " ORDER BY acid ASC LIMIT %d",
      sidCurrent, nLimit
    );
  }
  while( db_step(&q)==SQLITE_ROW ){
    const char *zRole = db_column_text(&q, 0);
    const char *zRoleLabel =
      zRole && fossil_strcmp(zRole,"user")==0 ? "You" :
      zRole && fossil_strcmp(zRole,"system")==0 ? "System" : "Agent";
    const char *zKind = db_column_text(&q, 1);
    const char *zProvider = db_column_text(&q, 2);
    const char *zModel = db_column_text(&q, 3);
    const char *zMeta = db_column_text(&q, 4);
    const char *zMsg = db_column_text(&q, 5);
    const char *zFeedback = db_column_text(&q, 6);
    int bPromptMeta = zRole && zKind
      && fossil_strcmp(zRole,"user")==0
      && fossil_strcmp(zKind,"prompt")==0
      && agent_chat_meta_context_enabled(zMeta);
    int retrievalQid = bPromptMeta ? agent_chat_meta_retrieval_qid(zMeta) : 0;
    blob_appendf(pOut, "<div style=\"margin-bottom:0.8em;\">\n");
    blob_appendf(pOut, "<b>%h:</b>", zRoleLabel);
    if( zProvider && zProvider[0] ){
      blob_appendf(pOut, " <span class=\"dimmed\">[%h%s%h]</span>", 
                   zProvider, (zModel&&zModel[0]?" / ":""), zModel);
    }
    if( zKind && zKind[0] ){
      blob_appendf(pOut, " <span class=\"dimmed\">{%h}</span>", zKind);
    }
    if( bPromptMeta ){
      blob_appendf(pOut, " <span class=\"dimmed\">[context=pool]</span>");
      if( retrievalQid>0 ){
        blob_appendf(pOut, " <span class=\"dimmed\">[<a href=\"%%R/agentui?sid=%d#retrieval-%d\" data-retrieval-qid=\"%d\">retrieval #%d</a>]</span>",
                     sidCurrent, retrievalQid, retrievalQid, retrievalQid);
      }
    }else if( zMeta && zMeta[0] ){
      blob_appendf(pOut, " <span class=\"dimmed\">meta=%h</span>", zMeta);
    }
    if( zFeedback && zFeedback[0] ){
      blob_appendf(pOut, " <span class=\"dimmed\">feedback=%h</span>", zFeedback);
    }
    blob_appendf(pOut, " <pre style=\"white-space:pre-wrap;display:inline;margin:0\">%h</pre>\n", zMsg);
    blob_appendf(pOut, "</div>\n");
  }
  db_finalize(&q);
}

static void agent_chat_render_history(int sidCurrent){
  Blob out = BLOB_INITIALIZER;
  agent_chat_render_history_to_blob(sidCurrent, &out);
  CX("%s", blob_str(&out));
  blob_reset(&out);
}

/*
** Add common submenu entries for the software management surfaces.
*/
static void agent_software_submenu(void){
  style_submenu_element("Overview", "%R/software");
  style_submenu_element("Timeline", "%R/timeline");
  style_submenu_element("Files", "%R/dir?ci=tip");
  style_submenu_element("Branches", "%R/brlist");
  style_submenu_element("Tags", "%R/taglist");
  style_submenu_element("Forum", "%R/forum");
  style_submenu_element("Chat", "%R/chat");
}

/*
** Add common submenu entries for the knowledge/data management surfaces.
*/
static void agent_knowledge_submenu(void){
  style_submenu_element("Overview", "%R/knowledge");
  style_submenu_element("Browse", "%R/knowledge-browser");
  style_submenu_element("Runs", "%R/knowledge-runs");
  style_submenu_element("Tickets", "%R/ticket");
  style_submenu_element("Wiki", "%R/wiki");
}

/*
** Add common submenu entries for the interactive agent surfaces.
*/
static void agent_console_submenu(int sidCurrent){
  style_submenu_element("Agent", "%R/agentui");
  if( sidCurrent>0 ){
    style_submenu_element("Session", "%R/agentui?sid=%d", sidCurrent);
  }
  style_submenu_element("Knowledge", "%R/knowledge");
  style_submenu_element("Software", "%R/software");
}

/*
** Add common submenu entries for the repository/system control surfaces.
*/
static void agent_system_submenu(void){
  style_submenu_element("Overview", "%R/system");
  style_submenu_element("Admin", "%R/setup");
  if( login_is_individual() ){
    style_submenu_element("Logout", "%R/logout");
  }else{
    style_submenu_element("Login", "%R/login");
  }
}

/*
** Render a compact HTML summary of the note pool by processing tier.
*/
static void agent_render_pool_html(void){
  int tier;
  if( !db_table_exists("repository","ai_note") ){
    @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);">
    @ <div class="dimmed">AI pool tables are not initialized for this repository yet.</div>
    @ </div>
    return;
  }
  for(tier=3; tier>=0; tier--){
    Stmt q;
    char *zProcess;
    int nNote;
    const char *zLabel =
      tier==3 ? "Tier 3: Atomic Records" :
      tier==2 ? "Tier 2: Curated Drafts" :
      tier==1 ? "Tier 1: Working Set" : "Tier 0: Raw Intake";
    zProcess = db_text("unknown",
      "SELECT coalesce(max(process_level),'unknown') FROM ai_note WHERE tier=%d",
      tier
    );
    nNote = db_int(0, "SELECT count(*) FROM ai_note WHERE tier=%d", tier);
    @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);">
    @ <div><b>%s(zLabel)</b></div>
    @ <div class="dimmed" style="margin:0.2em 0 0.5em 0;">%d(nNote) notes, process level %h(zProcess)</div>
    db_prepare(&q,
      "SELECT nid, coalesce(nullif(title,''), printf('note #%%d',nid)),"
      "       retrieval_count, coalesce(duplicate_of,0), coalesce(merged_into,0)"
      "  FROM ai_note"
      " WHERE tier=%d"
      " ORDER BY updated_at DESC, nid DESC"
      " LIMIT 4",
      tier
    );
    if( db_step(&q)==SQLITE_ROW ){
      do{
        int nid = db_column_int(&q, 0);
        const char *zTitle = db_column_text(&q, 1);
        int nRetrieve = db_column_int(&q, 2);
        int duplicateOf = db_column_int(&q, 3);
        int mergedInto = db_column_int(&q, 4);
        @ <div style="margin:0.2em 0 0.2em 0.5em;">
        @ %h(zTitle)
        @ <span class="dimmed">[#%d(nid), retrievals=%d(nRetrieve)</span>
        if( duplicateOf>0 ){
          @ <span class="dimmed">, duplicate_of=%d(duplicateOf)</span>
        }
        if( mergedInto>0 ){
          @ <span class="dimmed">, merged_into=%d(mergedInto)</span>
        }
        @ <span class="dimmed">]</span>
        @ </div>
      }while( db_step(&q)==SQLITE_ROW );
    }else{
      @ <div class="dimmed">No records in this tier yet.</div>
    }
    db_finalize(&q);
    fossil_free(zProcess);
    @ </div>
  }
}

/*
** Render recent persisted runs for the knowledge landing page.
*/
static void agent_render_recent_runs_html(int nLimit){
  Stmt q;
  if( nLimit<=0 ) nLimit = 5;
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);">
  @ <div style="font-weight:bold;margin-bottom:0.4em;">Recent Runs <span class="dimmed" style="font-weight:normal;">[<a href="%R/knowledge-runs">browse all</a>]</span></div>
  if( !db_table_exists("repository","agentrun") ){
    @ <div class="dimmed">No persisted run ledger yet.</div>
    @ </div>
    return;
  }
  db_prepare(&q,
    "SELECT runid, kind, name, status, coalesce(summary,''), mtime"
    "  FROM agentrun"
    " ORDER BY runid DESC"
    " LIMIT %d",
    nLimit
  );
  if( db_step(&q)==SQLITE_ROW ){
    do{
      int runid = db_column_int(&q,0);
      @ <div style="margin:0.35em 0;">
      @ <b><a href="%R/knowledge-run?runid=%d(runid)">#%d(runid)</a></b> %h(db_column_text(&q,1))
      @ <span class="dimmed">[%h(db_column_text(&q,2)) | %h(db_column_text(&q,3))]</span><br>
      @ <span class="dimmed">%h(db_column_text(&q,4))</span>
      @ </div>
    }while( db_step(&q)==SQLITE_ROW );
  }else{
    @ <div class="dimmed">No saved runs yet.</div>
  }
  db_finalize(&q);
  @ </div>
}

/*
** Render recent retrievals for the knowledge landing page.
*/
static void agent_render_recent_retrievals_html(int nLimit){
  Stmt q;
  if( nLimit<=0 ) nLimit = 5;
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);">
  @ <div style="font-weight:bold;margin-bottom:0.4em;">Recent Retrievals</div>
  if( !db_table_exists("repository","ai_retrieval") ){
    @ <div class="dimmed">No retrieval history yet.</div>
    @ </div>
    return;
  }
  db_prepare(&q,
    "SELECT qid, query_text, created_at,"
    "       (SELECT count(*) FROM ai_retrieval_note rn WHERE rn.qid=r.qid)"
    "  FROM ai_retrieval r"
    " ORDER BY qid DESC"
    " LIMIT %d",
    nLimit
  );
  if( db_step(&q)==SQLITE_ROW ){
    do{
      @ <div style="margin:0.35em 0;">
      @ <b>#%d(db_column_int(&q,0))</b> %h(db_column_text(&q,1))
      @ <span class="dimmed">[%h(db_column_text(&q,2)), %d(db_column_int(&q,3)) notes]</span>
      @ </div>
    }while( db_step(&q)==SQLITE_ROW );
  }else{
    @ <div class="dimmed">No retrievals yet.</div>
  }
  db_finalize(&q);
  @ </div>
}

/*
** Return a static human-readable label for an AI note tier.
*/
static const char *agent_note_tier_label(int tier){
  switch( tier ){
    case 3: return "Tier 3: Atomic Records";
    case 2: return "Tier 2: Curated Drafts";
    case 1: return "Tier 1: Working Set";
    default: return "Tier 0: Raw Intake";
  }
}

/*
** Build the SQL WHERE clause for the knowledge browser filters.
*/
static char *agent_knowledge_filter_clause(
  const char *zTier,
  const char *zSource,
  const char *zProcess,
  const char *zSearch,
  int showMerged
){
  Blob sql = BLOB_INITIALIZER;
  blob_append_sql(&sql, " WHERE 1");
  if( zTier && zTier[0] && fossil_isdigit(zTier[0]) ){
    int tier = atoi(zTier);
    if( tier>=0 && tier<=3 ){
      blob_append_sql(&sql, " AND coalesce(tier,0)=%d", tier);
    }
  }
  if( zSource && zSource[0] ){
    blob_append_sql(&sql, " AND coalesce(source_type,'')=%Q", zSource);
  }
  if( zProcess && zProcess[0] ){
    blob_append_sql(&sql, " AND coalesce(process_level,'')=%Q", zProcess);
  }
  if( !showMerged ){
    blob_append_sql(&sql, " AND coalesce(merged_into,0)=0");
  }
  if( zSearch && zSearch[0] ){
    char *zLike = mprintf("%%%s%%", zSearch);
    blob_append_sql(&sql,
      " AND (coalesce(title,'') LIKE %Q"
      "   OR coalesce(body,'') LIKE %Q"
      "   OR coalesce(source_ref,'') LIKE %Q)",
      zLike, zLike, zLike
    );
    fossil_free(zLike);
  }
  return blob_str(&sql);
}

/*
** Emit a best-effort source/artifact link for one knowledge note.
*/
static void agent_render_note_source_link(
  const char *zSourceType,
  int sourceId,
  const char *zSourceRef
){
  if( zSourceType==0 ) zSourceType = "";
  if( zSourceRef==0 ) zSourceRef = "";
  @ <span class="dimmed">source:</span> %h(zSourceType)
  if( zSourceRef[0] ){
    @ <span class="dimmed">| ref:</span>
    if( fossil_strcmp(zSourceType, "wiki")==0 ){
      @ <a href="%R/wiki?name=%T(zSourceRef)">%h(zSourceRef)</a>
    }else if( fossil_strcmp(zSourceType, "ticket")==0 ){
      @ <a href="%R/tktview/%T(zSourceRef)">%h(zSourceRef)</a>
    }else if( fossil_strcmp(zSourceType, "doc")==0 ){
      @ <a href="%R/doc/tip/%T(zSourceRef)">%h(zSourceRef)</a>
    }else if( fossil_strcmp(zSourceType, "technote")==0 ){
      @ <a href="%R/technote/%T(zSourceRef)">%h(zSourceRef)</a>
    }else{
      @ <code>%h(zSourceRef)</code>
    }
  }
  if( sourceId>0 ){
    @ <span class="dimmed">| id=%d(sourceId)</span>
  }
}

/*
** Emit an explicit durable artifact link for one knowledge note when present.
*/
static void agent_render_note_artifact_link(
  const char *zArtifactKind,
  const char *zArtifactRef,
  int artifactRid,
  const char *zArtifactPath,
  const char *zArtifactStatus
){
  char *zUuid = 0;
  if( zArtifactKind==0 ) zArtifactKind = "";
  if( zArtifactRef==0 ) zArtifactRef = "";
  if( zArtifactPath==0 ) zArtifactPath = "";
  if( zArtifactStatus==0 ) zArtifactStatus = "";
  if( zArtifactKind[0]==0 && zArtifactRef[0]==0 && artifactRid<=0
   && zArtifactPath[0]==0 ){
    return;
  }
  @ <div style="margin-top:0.35em;">
  @ <span class="dimmed">artifact:</span>
  if( fossil_strcmp(zArtifactKind, "wiki")==0 && zArtifactRef[0] ){
    @ <a href="%R/wiki?name=%T(zArtifactRef)">%h(zArtifactRef)</a>
  }else if( fossil_strcmp(zArtifactKind, "ticket")==0 && zArtifactRef[0] ){
    @ <a href="%R/tktview/%T(zArtifactRef)">%h(zArtifactRef)</a>
  }else if( fossil_strcmp(zArtifactKind, "technote")==0 && zArtifactRef[0] ){
    @ <a href="%R/technote/%T(zArtifactRef)">%h(zArtifactRef)</a>
  }else if(
    (fossil_strcmp(zArtifactKind, "doc")==0 || fossil_strcmp(zArtifactKind, "file")==0)
    && (zArtifactPath[0] || zArtifactRef[0])
  ){
    const char *zPath = zArtifactPath[0] ? zArtifactPath : zArtifactRef;
    @ <a href="%R/doc/tip/%T(zPath)">%h(zPath)</a>
  }else if( zArtifactRef[0] ){
    @ <code>%h(zArtifactRef)</code>
  }else if( artifactRid>0 ){
    zUuid = rid_to_uuid(artifactRid);
    if( zUuid && zUuid[0] ){
      @ <a href="%R/artifact/%S(zUuid)">artifact %d(artifactRid)</a>
    }else{
      @ <span class="dimmed">rid=%d(artifactRid)</span>
    }
    fossil_free(zUuid);
  }else if( zArtifactPath[0] ){
    @ <code>%h(zArtifactPath)</code>
  }
  if( zArtifactKind[0] ){
    @ <span class="dimmed">| kind=%h(zArtifactKind)</span>
  }
  if( zArtifactStatus[0] ){
    @ <span class="dimmed">| status=%h(zArtifactStatus)</span>
  }
  @ </div>
}

/*
** Emit recent retrieval history for a single note.
*/
static void agent_render_note_retrieval_history(int nid){
  Stmt q;
  int shown = 0;
  if( nid<=0 || !db_table_exists("repository","ai_retrieval_note")
   || !db_table_exists("repository","ai_retrieval") ){
    return;
  }
  db_prepare(&q,
    "SELECT r.qid, coalesce(r.query_text,''),"
    "       datetime(r.created_at,toLocal()), rn.rank"
    "  FROM ai_retrieval_note AS rn"
    "  JOIN ai_retrieval AS r ON r.qid=rn.qid"
    " WHERE rn.nid=%d"
    " ORDER BY r.qid DESC"
    " LIMIT 3",
    nid
  );
  while( db_step(&q)==SQLITE_ROW ){
    if( shown==0 ){
      @ <div style="margin-top:0.35em;"><span class="dimmed">recent retrievals:</span>
    }
    @ <div style="margin:0.15em 0 0 0.75em;">
    @ <a href="%R/agent-retrieval?qid=%d(db_column_int(&q,0))">retrieval #%d(db_column_int(&q,0))</a>
    @ <span class="dimmed">rank=%d(db_column_int(&q,3))</span>
    if( db_column_text(&q, 2) && db_column_text(&q, 2)[0] ){
      @ <span class="dimmed">[%h(db_column_text(&q,2))]</span>
    }
    if( db_column_text(&q, 1) && db_column_text(&q, 1)[0] ){
      @ <br><span class="dimmed">%h(db_column_text(&q,1))</span>
    }
    @ </div>
    shown++;
  }
  db_finalize(&q);
  if( shown>0 ){
    @ </div>
  }
}

/*
** WEBPAGE: software
**
** High-level landing page for SCM and collaboration interfaces.
*/
void software_page(void){
  login_check_credentials();
  if( !g.perm.Read ){
    login_needed(g.anon.Read);
    return;
  }
  style_set_current_feature("software");
  agent_software_submenu();
  style_header("Software Management");
  @ <div class="fossil-doc" data-title="Software Management">
  @ <p>This path groups the repository's software management interfaces:
  @ history, source browsing, branch and tag navigation, discussion, and
  @ project collaboration workflows.</p>
  @ <div style="display:grid;grid-template-columns:repeat(3,minmax(16em,1fr));gap:0.8em;">
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b><a href="%R/timeline">Timeline</a></b><br><span class="dimmed">repository history and check-in flow</span></div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b><a href="%R/dir?ci=tip">Files</a></b><br><span class="dimmed">browse current source tree state</span></div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b><a href="%R/brlist">Branches</a></b><br><span class="dimmed">active and historical branch structure</span></div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b><a href="%R/taglist">Tags</a></b><br><span class="dimmed">release and metadata tag navigation</span></div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b><a href="%R/forum">Forum</a></b><br><span class="dimmed">human discussion and coordination</span></div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b><a href="%R/chat">Chat</a></b><br><span class="dimmed">project chat and lightweight coordination</span></div>
  @ </div>
  style_finish_page();
}

/*
** WEBPAGE: system
**
** High-level landing page for repository administration and session control.
*/
void system_page(void){
  login_check_credentials();
  style_set_current_feature("system");
  agent_system_submenu();
  style_header("System Control");
  @ <div class="fossil-doc" data-title="System Control">
  @ <p>This path groups repository administration and authentication controls
  @ under one top-level tab.</p>
  @ <div style="display:grid;grid-template-columns:repeat(3,minmax(16em,1fr));gap:0.8em;">
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);">
  @ <b><a href="%R/setup">Admin</a></b><br>
  @ <span class="dimmed">repository setup, policy, users, and skin configuration</span>
  @ </div>
  if( login_is_individual() ){
    @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);">
    @ <b><a href="%R/logout">Logout</a></b><br>
    @ <span class="dimmed">end the current authenticated session</span>
    @ </div>
  }else{
    @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);">
    @ <b><a href="%R/login">Login</a></b><br>
    @ <span class="dimmed">authenticate and enter the control surface</span>
    @ </div>
  }
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);">
  @ <b><a href="%R/software">Software</a></b><br>
  @ <span class="dimmed">return to source and collaboration interfaces</span>
  @ </div>
  @ </div>
  @ </div>
  style_finish_page();
}

/*
** WEBPAGE: knowledge
**
** High-level landing page for the repository knowledge processing system.
*/
void knowledge_page(void){
  int totalNotes;
  int totalRetrievals;
  int totalRuns;
  int totalSessions;

  login_check_credentials();
  if( !g.perm.Read ){
    login_needed(g.anon.Read);
    return;
  }
  style_set_current_feature("knowledge");
  agent_knowledge_submenu();
  style_header("Knowledge System");
  totalNotes = db_table_exists("repository","ai_note")
             ? db_int(0, "SELECT count(*) FROM ai_note") : 0;
  totalRetrievals = db_table_exists("repository","ai_retrieval")
                  ? db_int(0, "SELECT count(*) FROM ai_retrieval") : 0;
  totalRuns = db_table_exists("repository","agentrun")
            ? db_int(0, "SELECT count(*) FROM agentrun") : 0;
  totalSessions = db_table_exists("repository","agentchat_session")
               ? db_int(0, "SELECT count(*) FROM agentchat_session") : 0;
  @ <div class="fossil-doc" data-title="Knowledge System">
  @ <p>This is the repository knowledge system. Inputs from chat, tickets, wiki,
  @ and other records stay in the pool, move through processing tiers, and are
  @ evaluated through retrieval and review instead of being treated as separate
  @ primary silos.</p>
  @ <div style="display:grid;grid-template-columns:repeat(4,minmax(10em,1fr));gap:0.8em;margin:0 0 1em 0;">
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b>%d(totalNotes)</b><br><span class="dimmed">pool records</span></div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b>%d(totalRetrievals)</b><br><span class="dimmed">retrieval runs</span></div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b>%d(totalRuns)</b><br><span class="dimmed">saved orchestration runs</span></div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b>%d(totalSessions)</b><br><span class="dimmed">interactive sessions</span></div>
  @ </div>
  @ <div style="display:grid;grid-template-columns:2fr 1fr;gap:1em;">
  @ <div>
  @ <div style="font-weight:bold;margin-bottom:0.5em;">Processing Tiers</div>
  @ <div style="display:grid;grid-template-columns:repeat(2,minmax(16em,1fr));gap:0.8em;">
  agent_render_pool_html();
  @ </div>
  @ </div>
  @ <div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);margin-bottom:1em;">
  @ <div style="font-weight:bold;margin-bottom:0.4em;">Data Management Interfaces</div>
  @ <div><a href="%R/ticket">Tickets</a> <span class="dimmed">structured issue and source records entering the pool</span></div>
  @ <div><a href="%R/wiki">Wiki</a> <span class="dimmed">curated narrative and knowledge pages</span></div>
  @ <div><a href="%R/agent-pool">Pool JSON</a> <span class="dimmed">machine-readable tier summary</span></div>
  @ </div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);margin-bottom:1em;">
  @ <div style="font-weight:bold;margin-bottom:0.4em;">Adjacent Paths</div>
  @ <div><a href="%R/software">Software Management</a> <span class="dimmed">SCM and collaboration surfaces</span></div>
  @ <div><a href="%R/agentui">Agent Console</a> <span class="dimmed">interactive retrieval and processing path</span></div>
  @ </div>
  agent_render_recent_retrievals_html(6);
  @ <div id="recent-runs" style="height:1em;"></div>
  agent_render_recent_runs_html(6);
  @ </div>
  @ </div>
  @ </div>
  style_finish_page();
}

/*
** WEBPAGE: knowledge-browser
**
** Browse all indexed knowledge elements with lightweight filters.
*/
void knowledge_browser_page(void){
  const char *zTier = P("tier");
  const char *zSource = P("source");
  const char *zProcess = P("process");
  const char *zSearch = P("q");
  int showMerged = PB("show_merged");
  int nLimit = atoi(PD("limit","40"));
  char *zWhere = 0;
  Blob sql = BLOB_INITIALIZER;
  int totalNotes = 0;
  int filteredNotes = 0;
  int hasArtifactKind;
  int hasArtifactRef;
  int hasArtifactRid;
  int hasArtifactPath;
  int hasArtifactStatus;
  Stmt q;
  const char *zTierAny = zTier && zTier[0] ? "" : " selected";
  const char *zTier0 = zTier && fossil_strcmp(zTier,"0")==0 ? " selected" : "";
  const char *zTier1 = zTier && fossil_strcmp(zTier,"1")==0 ? " selected" : "";
  const char *zTier2 = zTier && fossil_strcmp(zTier,"2")==0 ? " selected" : "";
  const char *zTier3 = zTier && fossil_strcmp(zTier,"3")==0 ? " selected" : "";

  if( nLimit<=0 ) nLimit = 40;
  if( nLimit>200 ) nLimit = 200;
  login_check_credentials();
  if( !g.perm.Read ){
    login_needed(g.anon.Read);
    return;
  }
  style_set_current_feature("knowledge");
  agent_knowledge_submenu();
  style_header("Knowledge Browser");
  @ <div class="fossil-doc" data-title="Knowledge Browser">
  @ <p>Browse indexed knowledge elements across the pool, including processing
  @ tier, source, retrieval history, and duplicate or merge lineage. This page
  @ is the browse surface; the overview at <a href="%R/knowledge">/knowledge</a>
  @ remains the dashboard.</p>
  if( !db_table_exists("repository","ai_note") ){
    @ <div style="border:1px solid #888;padding:0.8em;background:rgba(127,127,127,0.05);">
    @ <b>No indexed knowledge records yet.</b><br>
    @ <span class="dimmed">The repository does not have AI note tables yet, so
    @ there is nothing to browse. Initialize or use an AI path that creates
    @ `ai_note` records first.</span>
    @ </div>
    @ </div>
    style_finish_page();
    return;
  }
  hasArtifactKind = db_table_has_column("repository","ai_note","artifact_kind");
  hasArtifactRef = db_table_has_column("repository","ai_note","artifact_ref");
  hasArtifactRid = db_table_has_column("repository","ai_note","artifact_rid");
  hasArtifactPath = db_table_has_column("repository","ai_note","artifact_path");
  hasArtifactStatus = db_table_has_column("repository","ai_note","artifact_status");
  zWhere = agent_knowledge_filter_clause(
    zTier, zSource, zProcess, zSearch, showMerged
  );
  totalNotes = db_int(0, "SELECT count(*) FROM ai_note");
  blob_init(&sql, "SELECT count(*) FROM ai_note", -1);
  blob_append(&sql, zWhere, -1);
  filteredNotes = db_int(0, "%s", blob_sql_text(&sql));
  blob_reset(&sql);
  @ <form method="get" action="%R/knowledge-browser">
  @ <div style="border:1px solid #888;padding:0.8em;background:rgba(127,127,127,0.05);margin-bottom:1em;">
  @ <div style="display:grid;grid-template-columns:repeat(5,minmax(10em,1fr));gap:0.7em;align-items:end;">
  @ <label>Tier<br>
  @ <select name="tier">
  @ <option value=""%s(zTierAny)>All tiers</option>
  @ <option value="3"%s(zTier3)>Tier 3</option>
  @ <option value="2"%s(zTier2)>Tier 2</option>
  @ <option value="1"%s(zTier1)>Tier 1</option>
  @ <option value="0"%s(zTier0)>Tier 0</option>
  @ </select></label>
  @ <label>Source Type<br><input type="text" name="source" value="%h(zSource ? zSource : "")" placeholder="wiki, ticket, doc"></label>
  @ <label>Process Level<br><input type="text" name="process" value="%h(zProcess ? zProcess : "")" placeholder="raw, grouped, curated, atomic"></label>
  @ <label>Search<br><input type="text" name="q" value="%h(zSearch ? zSearch : "")" placeholder="title, body, or source ref"></label>
  @ <label>Limit<br><input type="number" min="1" max="200" name="limit" value="%d(nLimit)"></label>
  @ </div>
  @ <div style="margin-top:0.7em;">
  @ <label><input type="checkbox" name="show_merged" value="1"%s(showMerged ? " checked" : "")> include merged-away records</label>
  @ <input type="submit" value="Filter">
  @ <a href="%R/knowledge-browser" style="margin-left:0.7em;">Reset</a>
  @ </div>
  @ </div>
  @ </form>
  @ <div style="display:grid;grid-template-columns:repeat(4,minmax(10em,1fr));gap:0.8em;margin:0 0 1em 0;">
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b>%d(filteredNotes)</b><br><span class="dimmed">matching notes</span></div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b>%d(totalNotes)</b><br><span class="dimmed">indexed notes</span></div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b>%d(db_int(0,"SELECT count(*) FROM ai_note WHERE coalesce(duplicate_of,0)!=0"))</b><br><span class="dimmed">marked duplicates</span></div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b>%d(db_int(0,"SELECT count(*) FROM ai_note WHERE coalesce(merged_into,0)!=0"))</b><br><span class="dimmed">merged records</span></div>
  @ </div>
  @ <div style="margin:0 0 1em 0;" class="dimmed">
  @ Artifact links on this page are best-effort and currently inferred from
  @ `source_type`, `source_ref`, and `source_id`. Dedicated artifact reference
  @ fields are the next schema step.
  @ </div>
  blob_init(&sql,
    "SELECT nid, coalesce(tier,0),"
    "       coalesce(nullif(title,''), printf('note #%d',nid)),"
    "       substr(replace(replace(coalesce(body,''), char(13), ' '), char(10), ' '), 1, 220),"
    "       coalesce(source_type,''), coalesce(source_id,0), coalesce(source_ref,''),"
    "       coalesce(process_level,''), coalesce(retrieval_count,0),"
    "       coalesce(datetime(last_retrieved_at,toLocal()),''),"
    "       coalesce(duplicate_of,0), coalesce(merged_into,0),"
    "       coalesce(datetime(updated_at,toLocal()),'')",
    -1
  );
  blob_append_sql(&sql, ", %s",
                  hasArtifactKind ? "coalesce(artifact_kind,'')" : "''");
  blob_append_sql(&sql, ", %s",
                  hasArtifactRef ? "coalesce(artifact_ref,'')" : "''");
  blob_append_sql(&sql, ", %s",
                  hasArtifactRid ? "coalesce(artifact_rid,0)" : "0");
  blob_append_sql(&sql, ", %s",
                  hasArtifactPath ? "coalesce(artifact_path,'')" : "''");
  blob_append_sql(&sql, ", %s",
                  hasArtifactStatus ? "coalesce(artifact_status,'')" : "''");
  blob_append_sql(&sql, " FROM ai_note");
  blob_append(&sql, zWhere, -1);
  blob_append_sql(&sql,
    " ORDER BY coalesce(tier,0) DESC, coalesce(retrieval_count,0) DESC,"
    "          coalesce(updated_at,0) DESC, nid DESC"
    " LIMIT %d",
    nLimit
  );
  db_prepare(&q, "%s", blob_sql_text(&sql));
  blob_reset(&sql);
  if( db_step(&q)==SQLITE_ROW ){
    do{
      int nid = db_column_int(&q, 0);
      int tier = db_column_int(&q, 1);
      int sourceId = db_column_int(&q, 5);
      int duplicateOf = db_column_int(&q, 10);
      int mergedInto = db_column_int(&q, 11);
      @ <div style="border:1px solid #888;padding:0.8em;background:rgba(127,127,127,0.05);margin:0 0 0.9em 0;">
      @ <div style="display:flex;justify-content:space-between;gap:1em;flex-wrap:wrap;">
      @ <div>
      @ <b>%h(db_column_text(&q, 2))</b>
      @ <span class="dimmed">[#%d(nid)]</span><br>
      @ <span class="dimmed">%s(agent_note_tier_label(tier))</span>
      @ <span class="dimmed">| process=%h(db_column_text(&q, 7))</span>
      @ <span class="dimmed">| retrievals=%d(db_column_int(&q, 8))</span>
      @ </div>
      @ <div class="dimmed">updated %h(db_column_text(&q, 12))</div>
      @ </div>
      @ <div style="margin-top:0.35em;">%h(db_column_text(&q, 3))</div>
      @ <div style="margin-top:0.35em;">
      agent_render_note_source_link(
        db_column_text(&q, 4), sourceId, db_column_text(&q, 6)
      );
      if( db_column_text(&q, 9) && db_column_text(&q, 9)[0] ){
        @ <span class="dimmed">| last retrieved %h(db_column_text(&q, 9))</span>
      }
      @ </div>
      agent_render_note_artifact_link(
        db_column_text(&q, 13),
        db_column_text(&q, 14),
        db_column_int(&q, 15),
        db_column_text(&q, 16),
        db_column_text(&q, 17)
      );
      if( duplicateOf>0 || mergedInto>0 ){
        @ <div style="margin-top:0.35em;">
        if( duplicateOf>0 ){
          @ <span class="dimmed">duplicate_of=#%d(duplicateOf)</span>
        }
        if( mergedInto>0 ){
          if( duplicateOf>0 ){
            @ <span class="dimmed"> | </span>
          }
          @ <span class="dimmed">merged_into=#%d(mergedInto)</span>
        }
        @ </div>
      }
      agent_render_note_retrieval_history(nid);
      @ </div>
    }while( db_step(&q)==SQLITE_ROW );
  }else{
    @ <div style="border:1px solid #888;padding:0.8em;background:rgba(127,127,127,0.05);">
    @ No notes match the current filter set.
    @ </div>
  }
  db_finalize(&q);
  fossil_free(zWhere);
  @ </div>
  style_finish_page();
}

/*
** WEBPAGE: knowledge-runs
**
** Browse saved orchestration runs from the repository ledger.
*/
void knowledge_runs_page(void){
  Stmt q;
  const char *zKind = PD("kind","");
  int nLimit = atoi(PD("limit","40"));
  login_check_credentials();
  if( !g.perm.Read ){
    login_needed(g.anon.Read);
    return;
  }
  if( nLimit<=0 ) nLimit = 40;
  style_set_current_feature("knowledge");
  agent_knowledge_submenu();
  style_header("Knowledge Runs");
  @ <div class="fossil-doc" data-title="Knowledge Runs">
  @ <p>Saved orchestration runs preserve recipe output, verification results,
  @ diagnostics summaries, and other durable execution artifacts for later
  @ inspection.</p>
  @ <form method="get" action="%R/knowledge-runs"
  @  style="margin:0 0 1em 0;padding:0.7em;border:1px solid #888;background:rgba(127,127,127,0.05);">
  @ <label>Kind:
  @ <select name="kind">
  @ <option value=""%s(zKind[0]==0?" selected":"")>all</option>
  @ <option value="recipe"%s(fossil_strcmp(zKind,"recipe")==0?" selected":"")>recipe</option>
  @ <option value="verify"%s(fossil_strcmp(zKind,"verify")==0?" selected":"")>verify</option>
  @ <option value="diagnostics"%s(fossil_strcmp(zKind,"diagnostics")==0?" selected":"")>diagnostics</option>
  @ </select>
  @ </label>
  @ <label style="margin-left:0.7em;">Limit:
  @ <input type="number" min="1" max="200" name="limit" value="%d(nLimit)" style="width:5em;">
  @ </label>
  @ <input type="submit" value="Apply" style="margin-left:0.7em;">
  @ <a href="%R/knowledge-runs" style="margin-left:0.7em;">Reset</a>
  @ </form>
  if( !db_table_exists("repository","agentrun") ){
    @ <div class="dimmed">No persisted run ledger yet.</div>
    @ </div>
    style_finish_page();
    return;
  }
  db_prepare(&q,
    "SELECT runid,"
    "       datetime(created_at,toLocal()),"
    "       coalesce(nullif(kind,''),'(unset)'),"
    "       coalesce(nullif(name,''),'(unset)'),"
    "       coalesce(nullif(status,''),'(unset)'),"
    "       coalesce(nullif(summary,''),''),"
    "       coalesce(json_extract(payload,'$.recipe'),'')"
    "  FROM agentrun"
    " WHERE (%Q='' OR kind=%Q)"
    " ORDER BY runid DESC LIMIT %d",
    zKind, zKind, nLimit
  );
  if( db_step(&q)==SQLITE_ROW ){
    @ <div style="display:grid;gap:0.8em;">
    do{
      int runid = db_column_int(&q,0);
      const char *zCreated = db_column_text(&q,1);
      const char *zRunKind = db_column_text(&q,2);
      const char *zName = db_column_text(&q,3);
      const char *zStatus = db_column_text(&q,4);
      const char *zSummary = db_column_text(&q,5);
      const char *zRecipe = db_column_text(&q,6);
      @ <div style="border:1px solid #888;padding:0.8em;background:rgba(127,127,127,0.05);">
      @ <div><b><a href="%R/knowledge-run?runid=%d(runid)">Run #%d(runid)</a></b> %h(zRunKind)
      @ <span class="dimmed">[%h(zName) | %h(zStatus) | %h(zCreated)]</span></div>
      if( zRecipe && zRecipe[0] ){
        @ <div class="dimmed" style="margin-top:0.2em;">recipe=%h(zRecipe)</div>
      }
      if( zSummary && zSummary[0] ){
        @ <div style="margin-top:0.4em;">%h(zSummary)</div>
      }
      @ </div>
    }while( db_step(&q)==SQLITE_ROW );
    @ </div>
  }else{
    @ <div class="dimmed">No saved runs match this filter.</div>
  }
  db_finalize(&q);
  @ </div>
  style_finish_page();
}

/*
** WEBPAGE: knowledge-run
**
** Show the full metadata and payload for one persisted orchestration run.
*/
void knowledge_run_page(void){
  Stmt q;
  int runid = atoi(PD("runid","0"));
  login_check_credentials();
  if( !g.perm.Read ){
    login_needed(g.anon.Read);
    return;
  }
  if( runid<=0 ){
    cgi_redirectf("%R/knowledge-runs");
    return;
  }
  style_set_current_feature("knowledge");
  agent_knowledge_submenu();
  style_header("Knowledge Run");
  agent_run_create_tables();
  db_prepare(&q,
    "SELECT datetime(created_at,toLocal()),"
    "       coalesce(nullif(kind,''),'(unset)'),"
    "       coalesce(nullif(name,''),'(unset)'),"
    "       coalesce(nullif(status,''),'(unset)'),"
    "       coalesce(nullif(summary,''),''),"
    "       coalesce(payload,''),"
    "       coalesce(json_extract(payload,'$.recipe'),'') ,"
    "       coalesce(json_extract(payload,'$.provider'),'') ,"
    "       coalesce(json_extract(payload,'$.model'),'') ,"
    "       coalesce(json_extract(payload,'$.query'),'') ,"
    "       coalesce(json_extract(payload,'$.guidance_ref_count'),"
    "                json_extract(payload,'$.verification.overall_ok'),"
    "                '')"
    "  FROM agentrun WHERE runid=%d",
    runid
  );
  if( db_step(&q)!=SQLITE_ROW ){
    db_finalize(&q);
    style_finish_page();
    fossil_redirect_home();
    return;
  }
  @ <div class="fossil-doc" data-title="Knowledge Run">
  @ <div style="margin-bottom:1em;"><a href="%R/knowledge-runs">&larr; Back to runs</a></div>
  @ <div style="border:1px solid #888;padding:0.8em;background:rgba(127,127,127,0.05);margin-bottom:1em;">
  @ <div><b>Run #%d(runid)</b></div>
  @ <div class="dimmed">created=%h(db_column_text(&q,0)) kind=%h(db_column_text(&q,1)) name=%h(db_column_text(&q,2)) status=%h(db_column_text(&q,3))</div>
  @ <div style="margin-top:0.5em;">%h(db_column_text(&q,4))</div>
  @ </div>
  @ <div style="display:grid;grid-template-columns:repeat(2,minmax(16em,1fr));gap:0.8em;margin-bottom:1em;">
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b>recipe</b><br>%h(db_column_text(&q,6))</div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b>provider / model</b><br>%h(db_column_text(&q,7)) / %h(db_column_text(&q,8))</div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b>query</b><br>%h(db_column_text(&q,9))</div>
  @ <div style="border:1px solid #888;padding:0.7em;background:rgba(127,127,127,0.05);"><b>guidance refs or overall_ok</b><br>%h(db_column_text(&q,10))</div>
  @ </div>
  @ <div style="border:1px solid #888;padding:0.8em;background:rgba(127,127,127,0.05);">
  @ <div style="font-weight:bold;margin-bottom:0.4em;">Payload</div>
  @ <pre style="white-space:pre-wrap">%h(db_column_text(&q,5))</pre>
  @ </div>
  @ </div>
  db_finalize(&q);
  style_finish_page();
}

/*
** Emit a JSON object describing a chat session and its stored messages.
*/
static void agent_emit_history_json(int sidCurrent){
  Stmt q;
  const char *zTitle = "New Chat";
  const char *zProvider = agent_chat_session_provider(sidCurrent, "");
  const char *zModel = agent_chat_session_model(sidCurrent, "");
  if( sidCurrent>0 && db_table_exists("repository","agentchat_session") ){
    zTitle = db_text("New Chat",
      "SELECT coalesce(nullif(title,''),'New Chat') FROM agentchat_session"
      " WHERE sid=%d",
      sidCurrent
    );
  }
  CX("{\"sid\":%d,\"title\":%!j,\"provider\":%!j,\"model\":%!j,\"messages\":[",
     sidCurrent, zTitle, zProvider, zModel);
  if( sidCurrent>0 && db_table_exists("repository","agentchat") ){
    int first = 1;
    if( db_table_exists("repository","ai_chat_eval") ){
      db_prepare(&q,
        "SELECT c.acid, c.role, c.kind, c.provider, c.model, c.meta, c.msg,"
        "       coalesce(e.user_feedback,'')"
        "  FROM agentchat AS c"
        "  LEFT JOIN ai_chat_eval AS e ON e.sid=c.sid AND e.acid=c.acid"
        " WHERE c.sid=%d"
        " ORDER BY c.acid ASC",
        sidCurrent
      );
    }else{
      db_prepare(&q,
        "SELECT acid, role, kind, provider, model, meta, msg, ''"
        " FROM agentchat WHERE sid=%d"
        " ORDER BY acid ASC",
        sidCurrent
      );
    }
    while( db_step(&q)==SQLITE_ROW ){
      CX("%s{\"acid\":%d,\"role\":%!j,\"kind\":%!j,\"provider\":%!j,"
         "\"model\":%!j,\"meta\":%!j,\"msg\":%!j,\"feedback\":%!j}",
         first ? "" : ",",
         db_column_int(&q, 0),
         db_column_text(&q, 1),
         db_column_text(&q, 2),
         db_column_text(&q, 3),
         db_column_text(&q, 4),
         db_column_text(&q, 5),
         db_column_text(&q, 6),
         db_column_text(&q, 7));
      first = 0;
    }
    db_finalize(&q);
  }
  CX("]}\n");
}

/*
** Emit a JSON object describing ordered chat events for sidCurrent. If
** afterAcid is greater than zero, only events with acid>afterAcid are
** returned.
*/
static void agent_emit_events_json(int sidCurrent, int afterAcid){
  Stmt q;
  int first = 1;
  CX("{\"sid\":%d,\"after\":%d,\"events\":[", sidCurrent, afterAcid);
  if( sidCurrent>0 && db_table_exists("repository","agentchat") ){
    if( db_table_exists("repository","ai_chat_eval") ){
      db_prepare(&q,
        "SELECT c.acid, c.role, c.kind, c.provider, c.model, c.meta, c.msg,"
        "       coalesce(e.user_feedback,'')"
        "  FROM agentchat AS c"
        "  LEFT JOIN ai_chat_eval AS e ON e.sid=c.sid AND e.acid=c.acid"
        " WHERE c.sid=%d AND c.acid>%d"
        " ORDER BY c.acid ASC",
        sidCurrent, afterAcid
      );
    }else{
      db_prepare(&q,
        "SELECT acid, role, kind, provider, model, meta, msg, ''"
        " FROM agentchat WHERE sid=%d AND acid>%d"
        " ORDER BY acid ASC",
        sidCurrent, afterAcid
      );
    }
    while( db_step(&q)==SQLITE_ROW ){
      CX("%s{\"acid\":%d,\"role\":%!j,\"kind\":%!j,\"provider\":%!j,"
         "\"model\":%!j,\"meta\":%!j,\"msg\":%!j,\"feedback\":%!j}",
         first ? "" : ",",
         db_column_int(&q, 0),
         db_column_text(&q, 1),
         db_column_text(&q, 2),
         db_column_text(&q, 3),
         db_column_text(&q, 4),
         db_column_text(&q, 5),
         db_column_text(&q, 6),
         db_column_text(&q, 7));
      first = 0;
    }
    db_finalize(&q);
  }
  CX("]}\n");
}

/*
** Emit a JSON summary of the data pool by processing tier, including a small
** recent-note sample for each tier and duplicate/merge totals.
*/
static void agent_emit_pool_json(void){
  int tier;
  int dupCount = db_table_exists("repository","ai_note")
    ? db_int(0, "SELECT count(*) FROM ai_note WHERE coalesce(duplicate_of,0)!=0")
    : 0;
  int mergedCount = db_table_exists("repository","ai_note")
    ? db_int(0, "SELECT count(*) FROM ai_note WHERE coalesce(merged_into,0)!=0")
    : 0;
  int retrievalCount = db_table_exists("repository","ai_retrieval")
    ? db_int(0, "SELECT count(*) FROM ai_retrieval") : 0;
  CX("{\"tiers\":[");
  for(tier=3; tier>=0; tier--){
    Stmt q;
    int first = 1;
    int noteCount = db_table_exists("repository","ai_note")
      ? db_int(0,
          "SELECT count(*) FROM ai_note"
          " WHERE coalesce(tier,0)=%d AND coalesce(merged_into,0)=0",
          tier
        ) : 0;
    CX("%s{\"tier\":%d,\"label\":%!j,\"process_level\":%!j,"
       "\"note_count\":%d,\"recent_notes\":[",
       tier<3 ? "," : "",
       tier,
       tier==3 ? "Tier 3: Atomic" :
       tier==2 ? "Tier 2: Curated" :
       tier==1 ? "Tier 1: Working" : "Tier 0: Raw",
       tier==3 ? "atomic" :
       tier==2 ? "curated-draft" :
       tier==1 ? "working-note" : "raw-capture",
       noteCount);
    if( db_table_exists("repository","ai_note") ){
      db_prepare(&q,
        "SELECT nid, coalesce(nullif(title,''),'(untitled)'),"
        "       coalesce(nullif(process_level,''),'(unset)'),"
        "       coalesce(retrieval_count,0),"
        "       coalesce(duplicate_of,0),"
        "       coalesce(merged_into,0)"
        "  FROM ai_note"
        " WHERE coalesce(tier,0)=%d"
        "   AND coalesce(merged_into,0)=0"
        " ORDER BY updated_at DESC, nid DESC LIMIT 4",
        tier
      );
      while( db_step(&q)==SQLITE_ROW ){
        CX("%s{\"nid\":%d,\"title\":%!j,\"process_level\":%!j,"
           "\"retrieval_count\":%d,\"duplicate_of\":%d,\"merged_into\":%d}",
           first ? "" : ",",
           db_column_int(&q, 0),
           db_column_text(&q, 1),
           db_column_text(&q, 2),
           db_column_int(&q, 3),
           db_column_int(&q, 4),
           db_column_int(&q, 5));
        first = 0;
      }
      db_finalize(&q);
    }
    CX("]}");
  }
  CX("],\"duplicate_count\":%d,\"merged_count\":%d,\"retrieval_count\":%d}\n",
     dupCount, mergedCount, retrievalCount);
}

/*
** Emit JSON details for a single retrieval history row and its retrieved notes.
*/
static void agent_emit_retrieval_json(int qid){
  Stmt q;
  int first = 1;
  const char *zQuery = 0;
  const char *zCreated = 0;
  if( qid<=0 || !db_table_exists("repository","ai_retrieval")
   || !db_exists("SELECT 1 FROM ai_retrieval WHERE qid=%d", qid) ){
    CX("{\"error\":%!j}\n", "unknown retrieval qid");
    return;
  }
  zQuery = db_text("",
    "SELECT coalesce(query_text,'') FROM ai_retrieval WHERE qid=%d", qid
  );
  zCreated = db_text("",
    "SELECT datetime(created_at,toLocal()) FROM ai_retrieval WHERE qid=%d", qid
  );
  CX("{\"qid\":%d,\"query_text\":%!j,\"created_at\":%!j,\"notes\":[",
     qid, zQuery, zCreated);
  db_prepare(&q,
    "SELECT r.nid, r.rank, r.score, r.tier_weight, r.reinforcement_delta,"
    "       coalesce(nullif(n.title,''),'(untitled)'),"
    "       coalesce(n.tier,0),"
    "       coalesce(nullif(n.process_level,''),'(unset)'),"
    "       coalesce(n.retrieval_count,0),"
    "       coalesce(n.duplicate_of,0),"
    "       coalesce(n.merged_into,0),"
    "       coalesce(rv.duplication_status,''),"
    "       coalesce(rv.atomicity_status,''),"
    "       coalesce(rv.metadata_status,'')"
    "  FROM ai_retrieval_note AS r"
    "  JOIN ai_note AS n ON n.nid=r.nid"
    "  LEFT JOIN ai_review AS rv ON rv.qid=r.qid AND rv.nid=r.nid"
    " WHERE r.qid=%d"
    " ORDER BY r.rank ASC, r.nid ASC",
    qid
  );
  while( db_step(&q)==SQLITE_ROW ){
    CX("%s{\"nid\":%d,\"rank\":%d,\"score\":%.17g,\"tier_weight\":%.17g,"
       "\"reinforcement_delta\":%.17g,\"title\":%!j,\"tier\":%d,"
       "\"process_level\":%!j,\"retrieval_count\":%d,"
       "\"duplicate_of\":%d,\"merged_into\":%d,"
       "\"duplication_status\":%!j,\"atomicity_status\":%!j,"
       "\"metadata_status\":%!j}",
       first ? "" : ",",
       db_column_int(&q, 0),
       db_column_int(&q, 1),
       db_column_double(&q, 2),
       db_column_double(&q, 3),
       db_column_double(&q, 4),
       db_column_text(&q, 5),
       db_column_int(&q, 6),
       db_column_text(&q, 7),
       db_column_int(&q, 8),
       db_column_int(&q, 9),
       db_column_int(&q, 10),
       db_column_text(&q, 11),
       db_column_text(&q, 12),
       db_column_text(&q, 13));
    first = 0;
  }
  db_finalize(&q);
  CX("]}\n");
}

/*
** Return the most recent non-empty model recorded for sid, or zDefault.
*/
static const char *agent_chat_session_model(int sid, const char *zDefault){
  const char *zFromSession;
  if( sid<=0 ) return zDefault;
  if( db_table_exists("repository","agentchat_session") ){
    zFromSession = db_text(0,
      "SELECT model FROM agentchat_session"
      " WHERE sid=%d AND model IS NOT NULL AND model<>''",
      sid
    );
    if( zFromSession && zFromSession[0] ) return zFromSession;
  }
  if( sid<=0 || !db_table_exists("repository","agentchat") ) return zDefault;
  return db_text(zDefault,
    "SELECT model FROM agentchat"
    " WHERE sid=%d AND model IS NOT NULL AND model<>''"
    " ORDER BY acid DESC LIMIT 1",
    sid
  );
}

/*
** Return the most recent non-empty provider recorded for sid, or zDefault.
*/
static const char *agent_chat_session_provider(int sid, const char *zDefault){
  const char *zFromSession;
  if( sid<=0 ) return zDefault;
  if( db_table_exists("repository","agentchat_session") ){
    zFromSession = db_text(0,
      "SELECT provider FROM agentchat_session"
      " WHERE sid=%d AND provider IS NOT NULL AND provider<>''",
      sid
    );
    if( zFromSession && zFromSession[0] ) return zFromSession;
  }
  if( !db_table_exists("repository","agentchat") ) return zDefault;
  return db_text(zDefault,
    "SELECT provider FROM agentchat"
    " WHERE sid=%d AND provider IS NOT NULL AND provider<>''"
    " ORDER BY acid DESC LIMIT 1",
    sid
  );
}

/*
** Return the RID of the latest version of wiki page zPageName, or 0 if
** the page does not yet exist.
*/
static int agent_wiki_rid(const char *zPageName){
  return db_int(0,
    "SELECT x.rid FROM tag t, tagxref x"
    " WHERE x.tagid=t.tagid"
    "   AND t.tagname='wiki-%q'"
    " ORDER BY x.mtime DESC LIMIT 1",
    zPageName
  );
}

/*
** Append a plain-text summary of pending checkout changes to pOut.
*/
static int agent_changes_text(Blob *pOut, int vid, const char *zPrefix){
  Stmt q;
  int nChange = 0;

  /* vfile_check_signature(vid, 0); -- triggers UPDATE which is disallowed in web mode */
  db_prepare(&q,
    "SELECT pathname,"
    "       CASE"
    "         WHEN deleted THEN 'DELETED'"
    "         WHEN rid=0 THEN 'ADDED'"
    "         WHEN coalesce(origname!=pathname,0) THEN 'RENAMED'"
    "         WHEN chnged THEN 'EDITED'"
    "         ELSE 'CHANGED'"
    "       END"
    "  FROM vfile"
    " WHERE vid=%d"
    "   AND (chnged OR deleted OR rid=0 OR coalesce(origname!=pathname,0))"
    " ORDER BY pathname",
    vid
  );
  while( db_step(&q)==SQLITE_ROW ){
    blob_appendf(pOut, "%s%s %s\n",
      zPrefix,
      db_column_text(&q, 1),
      db_column_text(&q, 0)
    );
    nChange++;
  }
  db_finalize(&q);
  return nChange;
}

/*
** Print a concise summary of pending checkout changes.
*/
static void agent_changes_cmd(void){
  Blob out = BLOB_INITIALIZER;
  int vid;
  int nChange;

  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_fatal("no active checkout");
  }
  nChange = agent_changes_text(&out, vid, "");
  if( nChange==0 ){
    fossil_print("CLEAN\n");
  }else{
    fossil_print("%s", blob_str(&out));
  }
  blob_reset(&out);
}

/*
** Read agent-authored markdown from FILE or stdin.
*/
static void agent_read_body(Blob *pOut, int useFile, const char *zFile){
  if( useFile ){
    blob_read_from_file(pOut, zFile, ExtFILE);
  }else{
    blob_read_from_channel(pOut, stdin, -1);
  }
  if( blob_size(pOut)==0 ){
    fossil_fatal("empty wiki update content");
  }
}

/*
** Build a manager-facing wiki journal entry into pOut.
*/
static void agent_build_wiki_entry(
  Blob *pOut,
  const char *zTitle,
  const char *zStatus,
  Blob *pBody
){
  int vid;
  char *zProjectName;
  char *zUuid;
  char *zUuidShort;
  char *zDate;
  char *zBranch;
  Blob changes = BLOB_INITIALIZER;
  int nChange;

  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_fatal("no active checkout");
  }

  zProjectName = db_text("<unnamed>",
    "SELECT value FROM config WHERE name='project-name'"
  );
  zUuid = rid_to_uuid(vid);
  zUuidShort = mprintf("%.12s", zUuid);
  zDate = db_text("", "SELECT datetime('now') || ' UTC'");
  zBranch = branch_of_rid(vid);
  blob_zero(pOut);
  blob_appendf(pOut, "# %s\n\n", zProjectName);
  blob_appendf(pOut, "## %s\n\n", zTitle && zTitle[0] ? zTitle
                                                      : "Development Update");
  blob_appendf(pOut, "- Recorded: %s\n", zDate);
  if( zStatus && zStatus[0] ){
    blob_appendf(pOut, "- Status: %s\n", zStatus);
  }
  blob_appendf(pOut, "- Check-in: %s\n", zUuidShort);
  if( zBranch && zBranch[0] ){
    blob_appendf(pOut, "- Branch: %s\n", zBranch);
  }
  blob_appendf(pOut, "- Repository: %s\n\n", db_repository_filename());
  blob_appendf(pOut, "### Update\n\n%s\n", blob_str(pBody));
  nChange = agent_changes_text(&changes, vid, "- ");
  if( nChange>0 ){
    blob_appendf(pOut, "\n### Working Changes\n\n");
    blob_appendf(pOut, "%s", blob_str(&changes));
  }

  free(zProjectName);
  free(zUuid);
  free(zUuidShort);
  free(zDate);
  free(zBranch);
  blob_reset(&changes);
}

/*
** Print a concise repository file map for the current checkout.
*/
static void agent_repomap_cmd(void){
  Stmt q;
  int vid;

  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_fatal("no active checkout");
  }
  db_prepare(&q,
    "SELECT pathname FROM vfile WHERE vid=%d AND deleted=0 ORDER BY pathname",
    vid
  );
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("%s\n", db_column_text(&q, 0));
  }
  db_finalize(&q);
}

/*
** Create or update a wiki page with a manager-facing development entry.
*/
static void agent_wiki_sync_cmd(void){
  Blob content = BLOB_INITIALIZER;
  Blob body = BLOB_INITIALIZER;
  Blob merged = BLOB_INITIALIZER;
  const char *zPageName;
  const char *zTitle;
  const char *zStatus;
  Manifest *pWiki = 0;
  int dryRunFlag;
  int appendFlag;
  int rid;

  dryRunFlag = find_option("dry-run", 0, 0)!=0;
  appendFlag = find_option("append", 0, 0)!=0;
  zTitle = find_option("title", 0, 1);
  zStatus = find_option("status", 0, 1);
  verify_all_options();
  if( g.argc!=4 && g.argc!=5 ){
    usage("wiki-sync PAGENAME ?FILE? [--append] [--dry-run]"
          " [--title TEXT] [--status TEXT]");
  }
  zPageName = g.argv[3];
  if( !wiki_name_is_wellformed((const unsigned char *)zPageName) ){
    fossil_fatal("not a valid wiki page name: %s", zPageName);
  }
  agent_read_body(&body, g.argc==5, g.argv[4]);
  agent_build_wiki_entry(&content, zTitle, zStatus, &body);
  blob_reset(&body);
  rid = agent_wiki_rid(zPageName);
  if( appendFlag && rid>0 ){
    pWiki = manifest_get(rid, CFTYPE_WIKI, 0);
    if( pWiki && pWiki->zWiki && pWiki->zWiki[0] ){
      blob_appendf(&merged, "%s\n\n---\n\n%s", pWiki->zWiki, blob_str(&content));
      blob_reset(&content);
      content = merged;
      blob_zero(&merged);
    }
    manifest_destroy(pWiki);
  }
  if( dryRunFlag ){
    fossil_print("%s", blob_str(&content));
    blob_reset(&content);
    return;
  }
  wiki_cmd_commit(zPageName, rid, &content, "text/x-markdown", 1);
  fossil_print("Updated wiki page %s.\n", zPageName);
  blob_reset(&content);
}

/*
** Perform a weighted semantic search for zQuery and append relevant notes to
** pOut. Results reinforce future retrievals and trigger the review loop.
*/
static int agent_semantic_search(
  const char *zModel,
  const char *zQuery,
  int nLimit,
  Blob *pOut,
  int bVerbose,
  int *pQid
){
  Blob vQuery = BLOB_INITIALIZER;
  Stmt q;
  int qid = 0;
  int nHit = 0;

  if( !ai_is_enabled() ) return 0;
  if( agent_generate_embedding(zModel, zQuery, &vQuery)!=0 ){
    blob_reset(&vQuery);
    return 0;
  }
  ai_schema_ensure();
  qid = ai_retrieval_begin(0, zQuery);
  if( pQid ) *pQid = qid;

  db_prepare(&q,
    "SELECT s.nid, s.title, s.body, s.tier, s.weighted_score, s.tier_weight"
    "  FROM ("
    "    SELECT n.nid AS nid,"
    "           n.title AS title,"
    "           n.body AS body,"
    "           coalesce(n.tier,0) AS tier,"
    "           CASE coalesce(n.tier,0)"
    "             WHEN 3 THEN 0.35"
    "             WHEN 2 THEN 0.20"
    "             WHEN 1 THEN 0.10"
    "             ELSE 0.0"
    "           END AS tier_weight,"
    "           (vec_distance(v.vector, :vec)"
    "             - CASE coalesce(n.tier,0)"
    "                 WHEN 3 THEN 0.35"
    "                 WHEN 2 THEN 0.20"
    "                 WHEN 1 THEN 0.10"
    "                 ELSE 0.0"
    "               END"
    "             - MIN(coalesce(n.artifact_weight,0.05),0.30)"
    "             - (MIN(coalesce(n.heat,1.0),25.0)*0.02)"
    "             - (MIN(coalesce(n.retrieval_count,0),50)*0.01)"
    "           ) AS weighted_score"
    "      FROM ai_vector v, ai_note n"
    "     WHERE v.source_type='note'"
    "       AND v.source_id=n.nid"
    "       AND coalesce(n.merged_into,0)=0"
    "  ) AS s"
    " ORDER BY s.weighted_score ASC, s.tier DESC, s.nid DESC"
    " LIMIT %d",
    nLimit
  );
  db_bind_blob(&q, ":vec", &vQuery);
  while( db_step(&q)==SQLITE_ROW ){
    int nid = db_column_int(&q, 0);
    const char *zTitle = db_column_text(&q, 1);
    const char *zBody = db_column_text(&q, 2);
    int tier = db_column_int(&q, 3);
    double rScore = db_column_double(&q, 4);
    double rTierWeight = db_column_double(&q, 5);
    double rDelta = ai_note_record_retrieval(
      qid, nid, ++nHit, rScore, rTierWeight
    );
    if( bVerbose ){
      blob_appendf(
        pOut,
        "--- Note %d: %s ---\n"
        "tier: %d\n"
        "score: %.4f\n"
        "reinforcement: +%.2f\n\n"
        "%s\n\n",
        nid, zTitle ? zTitle : "(untitled)", tier, rScore, rDelta,
        zBody ? zBody : ""
      );
    }else{
      blob_appendf(pOut, "\n--- Relevant Note (T%d): %s ---\n%s\n",
                   tier, zTitle ? zTitle : "(untitled)", zBody ? zBody : "");
    }
  }
  db_finalize(&q);
  if( nHit>0 ) ai_retrieval_review(qid);
  blob_reset(&vQuery);
  return nHit;
}

/*
** Assemble a context summary of the current repository state into pOut.
** Returns non-zero if any useful context was added.
*/
static const char *agent_prompt_fragment(const char *zKey, const char *zDefault){
  static Blob config = BLOB_INITIALIZER;
  if( blob_size(&config)==0 ){
    char *zPath = mprintf("%scfg/agent_prompts.json", g.zLocalRoot);
    blob_read_from_file(&config, zPath, ExtFILE);
    fossil_free(zPath);
  }
  if( blob_size(&config)>0 ){
    /* Simple JSON extraction for fragments */
    char *zSearch = mprintf("\"%s\":\"", zKey);
    char *zPos = strstr(blob_str(&config), zSearch);
    fossil_free(zSearch);
    if( zPos ){
      char *zEnd;
      zPos += strlen(zKey) + 4;
      zEnd = strchr(zPos, '\"');
      if( zEnd ){
        int n = (int)(zEnd - zPos);
        return mprintf("%.*s", n, zPos);
      }
    }
  }
  return zDefault;
}

static int agent_assemble_context(
  Blob *pOut,
  const char *zModel,
  const char *zQuery,
  int *pRetrievalQid
){
  int vid;
  Stmt q;
  int nAdded = 0;
  vid = db_table_exists("localdb", "vvar") ? db_lget_int("checkout", 0) : 0;
  if( vid ){
    int nFile = 0;
    blob_appendf(pOut, "%s\n", agent_prompt_fragment("context_header", "--- REPOSITORY CONTEXT ---"));
    blob_appendf(pOut, "%s\n", agent_prompt_fragment("file_structure_header", "File Structure (top 100 files):"));
    db_prepare(&q,
      "SELECT pathname FROM vfile WHERE vid=%d AND deleted=0 ORDER BY pathname",
      vid
    );
    while( db_step(&q)==SQLITE_ROW && nFile<100 ){
      blob_appendf(pOut, "  %s\n", db_column_text(&q, 0));
      nFile++;
    }
    db_finalize(&q);
    if( nFile>=100 ) blob_appendf(pOut, "  ... (truncated)\n");
    blob_appendf(pOut, "\n%s\n", agent_prompt_fragment("pending_changes_header", "Pending Changes:"));
    if( agent_changes_text(pOut, vid, "  ")==0 ){
      blob_appendf(pOut, "  (none)\n");
    }
    nAdded = 1;
  }

  if( zQuery && zQuery[0] ){
    int nBefore = blob_size(pOut);
    int qid = 0;
    if( !nAdded ){
      blob_appendf(pOut, "%s\n", agent_prompt_fragment("context_header", "--- REPOSITORY CONTEXT ---"));
    }
    agent_semantic_search(zModel, zQuery, 3, pOut, 0, &qid);
    if( pRetrievalQid ) *pRetrievalQid = qid;
    if( qid>0 ){
      Stmt qR;
      int nExpanded = 0;
      db_prepare(&qR,
        "SELECT n.nid, n.title, n.body, n.tier"
        "  FROM ai_note n, ai_note_link l"
        " WHERE l.from_nid IN (SELECT nid FROM ai_retrieval_note WHERE qid=%d)"
        "   AND l.to_nid=n.nid"
        "   AND n.nid NOT IN (SELECT nid FROM ai_retrieval_note WHERE qid=%d)"
        " UNION "
        "SELECT n.nid, n.title, n.body, n.tier"
        "  FROM ai_note n, ai_note_link l"
        " WHERE l.to_nid IN (SELECT nid FROM ai_retrieval_note WHERE qid=%d)"
        "   AND l.from_nid=n.nid"
        "   AND n.nid NOT IN (SELECT nid FROM ai_retrieval_note WHERE qid=%d)"
        " ORDER BY n.tier DESC LIMIT 3",
        qid, qid, qid, qid
      );
      while( db_step(&qR)==SQLITE_ROW ){
        if( nExpanded==0 ){
          blob_appendf(pOut, "\n%s\n", agent_prompt_fragment("graph_expansion_header", "Knowledge Graph Expansion (related info):"));
        }
        blob_appendf(pOut, "--- Related (T%d): %s ---\n%s\n",
          db_column_int(&qR, 3), db_column_text(&qR, 1), db_column_text(&qR, 2)
        );
        nExpanded++;
      }
      db_finalize(&qR);
    }
    if( blob_size(pOut)>nBefore ) nAdded = 1;
  }
  if( nAdded ){
    blob_appendf(pOut, "%s\n\n", agent_prompt_fragment("context_footer", "--- END CONTEXT ---"));
  }else{
    blob_reset(pOut);
  }
  return nAdded;
}

/*
** Invoke the configured agent backend and store its reply in pReply.
**
** Returns 0 on success and non-zero on error.
*/
static void agent_strip_ansi(Blob *pText);
/*
** Chunk handler for streaming output.
*/
typedef void (*agent_chunk_handler)(const char *zChunk, int nChunk, void *pApp);

/*
** SSE chunk handler: emits text as a "data:" SSE event.
*/
static void agent_sse_handler(const char *zChunk, int nChunk, void *pApp){
  if( nChunk<=0 ) return;
  CX("data: %!j\n\n", zChunk);
  fflush(stdout);
}

static int agent_run_backend_core(
  const char *zProvider,
  const char *zModel,
  const char *zPrompt,
  Blob *pReply,
  Blob *pErr,
  agent_chunk_handler xChunk,
  void *pApp
){
  Blob cmd = BLOB_INITIALIZER;
  Blob envCmd = BLOB_INITIALIZER;
  FILE *in;
  FILE *out = 0;
  int fdIn = -1;
  int childPid = 0;
  int rc;
  const char *zCmdTmpl = agent_command_template();

  if( pReply ) blob_zero(pReply);
  blob_zero(pErr);
  if( agent_validate_provider_model(zProvider, zModel, pErr) ){
    return 1;
  }
  agent_expand_command(&cmd, zCmdTmpl, zModel);
  agent_prepare_command(&envCmd, "chat", zProvider, zModel, &cmd);
  rc = popen2(blob_str(&envCmd), &fdIn, &out, &childPid, 0);
  if( rc!=0 || fdIn<0 || out==0 ){
    blob_appendf(pErr, "unable to run configured agent command");
    blob_reset(&cmd);
    blob_reset(&envCmd);
    return 1;
  }
  fprintf(out, "%s", zPrompt);
  fclose(out);
  out = 0;
  in = fdopen(fdIn, "rb");
  if( in==0 ){
    pclose2(fdIn, out, childPid);
    blob_appendf(pErr, "unable to read output from configured agent command");
    blob_reset(&cmd);
    blob_reset(&envCmd);
    return 1;
  }
  if( xChunk ){
    char zBuf[1024];
    int n;
    while( (n = fread(zBuf, 1, sizeof(zBuf)-1, in))>0 ){
      zBuf[n] = 0;
      xChunk(zBuf, n, pApp);
      if( pReply ) blob_append(pReply, zBuf, n);
    }
  }else{
    blob_read_from_channel(pReply, in, -1);
  }
  pclose2(fdIn, out, childPid);
  if( pReply ){
    agent_strip_ansi(pReply);
    agent_strip_prefix_noise(pReply);
    blob_trim(pReply);
    if( blob_size(pReply)==0 ){
      if( pErr ){
        blob_appendf(pErr, "agent backend returned an empty reply");
      }
      blob_reset(&cmd);
      blob_reset(&envCmd);
      return 1;
    }
  }
  blob_reset(&cmd);
  blob_reset(&envCmd);
  return 0;
}

static int agent_run_backend(
  const char *zProvider,
  const char *zModel,
  const char *zPrompt,
  Blob *pReply,
  Blob *pErr
){
  return agent_run_backend_core(zProvider, zModel, zPrompt, pReply, pErr, 0, 0);
}

/*
** Print or emit a verification summary for the current effective agent
** configuration. With --json, the output is JSON. Without it, a human-readable
** summary is emitted. Optional smoke flags execute the configured backend(s).
*/
static void agent_verify_cmd(void){
  const char *zSource = 0;
  char *zChatProvider = mprintf("%s", agent_chat_provider());
  char *zChatModel = mprintf("%s", agent_default_model());
  char *zChatCmd = mprintf("%s", agent_command_template());
  char *zEmbedProvider = mprintf("%s", agent_embedding_provider());
  char *zEmbedModel = mprintf("%s", agent_embedding_model());
  char *zEmbedCmd = mprintf("%s", agent_embedding_template());
  char *zChatModelError = 0;
  char *zChatCommandPath = 0;
  char *zChatCommandDetail = 0;
  char *zEmbedModelError = 0;
  char *zEmbedCommandPath = 0;
  char *zEmbedCommandDetail = 0;
  Blob smokeReply = BLOB_INITIALIZER;
  Blob smokeErr = BLOB_INITIALIZER;
  Blob vSmoke = BLOB_INITIALIZER;
  Blob json = BLOB_INITIALIZER;
  int jsonFlag = find_option("json", 0, 0)!=0;
  int saveFlag = find_option("save", 0, 0)!=0;
  int chatSmokeFlag = find_option("chat-smoke", 0, 0)!=0;
  int embedSmokeFlag = find_option("embed-smoke", 0, 0)!=0;
  int chatProviderKnown = agent_provider_is_known(zChatProvider);
  int embeddingProviderKnown = agent_provider_is_known(zEmbedProvider);
  int chatModelValid = agent_validate_provider_model_ex(
    zChatProvider, zChatModel, &zChatModelError
  );
  int chatCommandReady = agent_command_is_ready(
    zChatCmd, &zChatCommandPath, &zChatCommandDetail
  );
  int embeddingAvailable = agent_embedding_is_available();
  int embeddingModelValid = agent_validate_provider_model_ex(
    zEmbedProvider, zEmbedModel, &zEmbedModelError
  );
  int embeddingCommandReady = agent_embedding_backend_ready(
    zEmbedProvider, zEmbedModel, zEmbedCmd,
    &zEmbedCommandPath, &zEmbedCommandDetail
  );
  int chatSmokeOk = 0;
  int embedSmokeOk = 0;
  const char *zChatSmoke = "skipped";
  const char *zEmbedSmoke = "skipped";
  int chatOk = chatProviderKnown && chatModelValid && chatCommandReady;
  int embeddingOk = embeddingProviderKnown && embeddingModelValid
                 && embeddingAvailable && embeddingCommandReady;
  int overallOk;

  verify_all_options();
  zSource = agent_config_source();
  if( chatSmokeFlag && chatOk ){
    chatSmokeOk = agent_run_backend(
      zChatProvider, zChatModel, "verify", &smokeReply, &smokeErr
    )==0;
    zChatSmoke = chatSmokeOk ? "ok" : blob_str(&smokeErr);
    blob_reset(&smokeReply);
    blob_reset(&smokeErr);
  }else if( chatSmokeFlag ){
    zChatSmoke = "skipped (chat not ready)";
  }
  if( embedSmokeFlag && embeddingOk ){
    embedSmokeOk = agent_generate_embedding(zEmbedModel, "verify", &vSmoke)==0;
    zEmbedSmoke = embedSmokeOk ? "ok" : "embedding generation failed";
    blob_reset(&vSmoke);
  }else if( embedSmokeFlag ){
    zEmbedSmoke = "skipped (embedding not ready)";
  }
  overallOk = chatOk && embeddingOk
           && (!chatSmokeFlag || chatSmokeOk)
           && (!embedSmokeFlag || embedSmokeOk);
  blob_appendf(&json, "{\"source\":%!j,"
               "\"chat\":{\"provider\":%!j,\"model\":%!j,"
               "\"provider_known\":%d,\"model_valid\":%d,"
               "\"model_error\":%!j,\"command_ready\":%d,"
               "\"command_path\":%!j,\"command_detail\":%!j,"
               "\"ok\":%d,\"smoke_requested\":%d,\"smoke_ok\":%d,"
               "\"smoke_detail\":%!j},"
               "\"embedding\":{\"provider\":%!j,\"model\":%!j,"
               "\"provider_known\":%d,\"model_valid\":%d,"
               "\"model_error\":%!j,\"available\":%d,"
               "\"command_ready\":%d,\"command_path\":%!j,"
               "\"command_detail\":%!j,\"ok\":%d,"
               "\"smoke_requested\":%d,\"smoke_ok\":%d,"
               "\"smoke_detail\":%!j},"
               "\"overall_ok\":%d}",
    zSource,
    zChatProvider, zChatModel,
    chatProviderKnown, chatModelValid,
    zChatModelError ? zChatModelError : "",
    chatCommandReady,
    zChatCommandPath ? zChatCommandPath : "",
    zChatCommandDetail ? zChatCommandDetail : "",
    chatOk, chatSmokeFlag, chatSmokeOk, zChatSmoke,
    zEmbedProvider, zEmbedModel,
    embeddingProviderKnown, embeddingModelValid,
    zEmbedModelError ? zEmbedModelError : "",
    embeddingAvailable,
    embeddingCommandReady,
    zEmbedCommandPath ? zEmbedCommandPath : "",
    zEmbedCommandDetail ? zEmbedCommandDetail : "",
    embeddingOk, embedSmokeFlag, embedSmokeOk, zEmbedSmoke,
    overallOk
  );
  if( jsonFlag ){
    if( saveFlag ){
      int runid = agent_run_record(
        "verify", "verify",
        overallOk ? "ok" : "not-ok",
        overallOk ? "verify overall ok" : "verify reported failures",
        blob_str(&json)
      );
      fossil_print("{\"saved_run_id\":%d,\"verify\":%s}\n", runid, blob_str(&json));
    }else{
      fossil_print("%s\n", blob_str(&json));
    }
  }else{
    fossil_print("source: %s\n", zSource);
    fossil_print("chat:\n");
    fossil_print("  provider: %s\n", zChatProvider && zChatProvider[0] ? zChatProvider : "(unset)");
    fossil_print("  model: %s\n", zChatModel && zChatModel[0] ? zChatModel : "(unset)");
    fossil_print("  provider-known: %s\n", chatProviderKnown ? "yes" : "no");
    fossil_print("  model-valid: %s\n", chatModelValid ? "yes" : "no");
    if( zChatModelError && zChatModelError[0] ){
      fossil_print("  model-error: %s\n", zChatModelError);
    }
    fossil_print("  command-ready: %s\n", chatCommandReady ? "yes" : "no");
    fossil_print("  command-path: %s\n",
                 zChatCommandPath && zChatCommandPath[0] ? zChatCommandPath : "(unresolved)");
    fossil_print("  command-detail: %s\n",
                 zChatCommandDetail && zChatCommandDetail[0] ? zChatCommandDetail : "(none)");
    fossil_print("  smoke: %s\n", zChatSmoke);
    fossil_print("  ok: %s\n", chatOk ? "yes" : "no");
    fossil_print("embedding:\n");
    fossil_print("  provider: %s\n", zEmbedProvider && zEmbedProvider[0] ? zEmbedProvider : "(unset)");
    fossil_print("  model: %s\n", zEmbedModel && zEmbedModel[0] ? zEmbedModel : "(unset)");
    fossil_print("  provider-known: %s\n", embeddingProviderKnown ? "yes" : "no");
    fossil_print("  model-valid: %s\n", embeddingModelValid ? "yes" : "no");
    if( zEmbedModelError && zEmbedModelError[0] ){
      fossil_print("  model-error: %s\n", zEmbedModelError);
    }
    fossil_print("  available: %s\n", embeddingAvailable ? "yes" : "no");
    fossil_print("  command-ready: %s\n", embeddingCommandReady ? "yes" : "no");
    fossil_print("  command-path: %s\n",
                 zEmbedCommandPath && zEmbedCommandPath[0] ? zEmbedCommandPath : "(unresolved)");
    fossil_print("  command-detail: %s\n",
                 zEmbedCommandDetail && zEmbedCommandDetail[0] ? zEmbedCommandDetail : "(none)");
    fossil_print("  smoke: %s\n", zEmbedSmoke);
    fossil_print("  ok: %s\n", embeddingOk ? "yes" : "no");
    fossil_print("overall: %s\n", overallOk ? "ok" : "fail");
    if( saveFlag ){
      int runid = agent_run_record(
        "verify", "verify",
        overallOk ? "ok" : "not-ok",
        overallOk ? "verify overall ok" : "verify reported failures",
        blob_str(&json)
      );
      fossil_print("saved-run: %d\n", runid);
    }
  }
  blob_reset(&json);
  fossil_free((char*)zSource);
  fossil_free(zChatModelError);
  fossil_free(zChatCommandPath);
  fossil_free(zChatCommandDetail);
  fossil_free(zEmbedModelError);
  fossil_free(zEmbedCommandPath);
  fossil_free(zEmbedCommandDetail);
  fossil_free(zChatProvider);
  fossil_free(zChatModel);
  fossil_free(zChatCmd);
  fossil_free(zEmbedProvider);
  fossil_free(zEmbedModel);
  fossil_free(zEmbedCmd);
}

/*
** Remove ANSI/VT100 escape sequences from CLI output so the web UI gets
** readable text instead of terminal control codes.
*/
static void agent_strip_ansi(Blob *pText){
  char *z = blob_buffer(pText);
  int n = blob_size(pText);
  int i;
  int j = 0;

  for(i=0; i<n; i++){
    unsigned char c = (unsigned char)z[i];
    if( c==0x1b && i+1<n ){
      unsigned char c1 = (unsigned char)z[i+1];
      if( c1=='[' ){
        i += 2;
        while( i<n ){
          c = (unsigned char)z[i];
          if( c>=0x40 && c<=0x7e ) break;
          i++;
        }
        continue;
      }else if( c1==']' ){
        i += 2;
        while( i<n ){
          c = (unsigned char)z[i];
          if( c==0x07 ) break;
          if( c==0x1b && i+1<n && z[i+1]=='\\' ){
            i++;
            break;
          }
          i++;
        }
        continue;
      }
    }
    z[j++] = z[i];
  }
  blob_resize(pText, j);
}

/*
** Drop any leading spinner glyphs or other console noise which may remain
** after ANSI escapes are removed.
*/
static void agent_strip_prefix_noise(Blob *pText){
  char *z = blob_buffer(pText);
  int n = blob_size(pText);
  int i = 0;

  while( i<n ){
    unsigned char c = (unsigned char)z[i];
    if( c>0x20 && c<0x7f ) break;
    i++;
  }
  if( i>0 && i<n ){
    memmove(z, z+i, n-i);
    blob_resize(pText, n-i);
  }
}

/*
** Generate an embedding for zText using the configured embedding backend.
** Returns 0 on success, non-zero on error.
** The result is stored as an array of floats in pOut.
*/
static int agent_generate_embedding(
  const char *zModel,
  const char *zText,
  Blob *pOut
){
  Blob cmd = BLOB_INITIALIZER;
  Blob envCmd = BLOB_INITIALIZER;
  Blob reply = BLOB_INITIALIZER;
  const char *zCmdTmpl = agent_embedding_template();
  char *z;
  FILE *p = 0;
  FILE *pOutToChild = 0;
  int fdFromChild = -1;
  int childPid = 0;
  int rc;
  Blob err = BLOB_INITIALIZER;
  const char *zProvider = agent_embedding_provider();

  if( agent_validate_provider_model(zProvider, zModel, &err) ){
    blob_reset(&err);
    return 1;
  }
  blob_reset(&err);

  if( zCmdTmpl[0] ){
    agent_expand_command(&cmd, zCmdTmpl, zModel);
    agent_prepare_command(&envCmd, "embed", zProvider, zModel, &cmd);
    rc = popen2(blob_str(&envCmd), &fdFromChild, &pOutToChild, &childPid, 0);
    if( rc!=0 || fdFromChild<0 || pOutToChild==0 ){
      blob_reset(&cmd);
      blob_reset(&envCmd);
      return 1;
    }
    fprintf(pOutToChild, "%s", zText);
    fclose(pOutToChild);
    pOutToChild = 0;
    p = fdopen(fdFromChild, "rb");
  }else{
    blob_appendf(&cmd, "curl -s -X POST http://localhost:11434/api/embed "
                       "-H \"Content-Type: application/json\" -d ");
    {
      Blob json = BLOB_INITIALIZER;
      blob_appendf(&json, "{\"model\":%!j, \"input\":%!j}", zModel, zText);
      blob_append_sql(&cmd, "%$", blob_str(&json));
      blob_reset(&json);
    }
    p = popen(blob_str(&cmd), "r");
  }
  if( p==0 ){
    blob_reset(&cmd);
    blob_reset(&envCmd);
    return 1;
  }
  blob_read_from_channel(&reply, p, -1);
  if( zCmdTmpl[0] ){
    pclose2(fdFromChild, pOutToChild, childPid);
  }else{
    pclose(p);
  }
  blob_reset(&cmd);
  blob_reset(&envCmd);

  /* Minimalist JSON parsing for Ollama responses. */
  z = strstr(blob_str(&reply), "\"embedding\":[");
  if( z==0 ){
    z = strstr(blob_str(&reply), "\"embeddings\":[[");
    if( z ) z += 15;
  }
  if( z==0 ){
    z = blob_str(&reply);
    while( z && *z ){
      char *zEnd;
      float f = (float)strtod(z, &zEnd);
      if( zEnd==z ) break;
      blob_append(pOut, (char*)&f, sizeof(f));
      z = zEnd;
      while( *z && (*z==',' || *z==' ' || *z=='\n' || *z=='\r' || *z=='\t') ){
        z++;
      }
    }
    if( blob_size(pOut)==0 ){
      blob_reset(&reply);
      return 1;
    }
    blob_reset(&reply);
    return 0;
  }
  if( fossil_strncmp(z, "\"embedding\":[", 13)==0 ){
    z += 13;
  }
  while( *z && *z!=']' ){
    float f = (float)strtod(z, &z);
    blob_append(pOut, (char*)&f, sizeof(f));
    while( *z && (*z==',' || *z==' ' || *z=='\n' || *z=='\r') ) z++;
  }
  blob_reset(&reply);
  return 0;
}

/*
** Generate embeddings for all notes that don't have them yet.
*/
static void agent_semantic_index_cmd(void){
  const char *zModel = agent_embedding_model();
  Stmt q1, q2;
  int n = 0;

  ai_require_enabled();
  db_prepare(&q1,
    "SELECT n.nid, n.title, n.body"
    "  FROM ai_note AS n"
    "  LEFT JOIN ai_vector AS v"
    "    ON v.source_type='note' AND v.source_id=n.nid"
    " WHERE v.source_id IS NULL"
    "   AND coalesce(n.merged_into,0)=0"
  );
  while( db_step(&q1)==SQLITE_ROW ){
    int nid = db_column_int(&q1, 0);
    const char *zTitle = db_column_text(&q1, 1);
    const char *zBody = db_column_text(&q1, 2);
    Blob v = BLOB_INITIALIZER;
    Blob text = BLOB_INITIALIZER;

    blob_appendf(&text, "%s\n%s", zTitle, zBody);
    if( agent_generate_embedding(zModel, blob_str(&text), &v)==0 ){
      db_prepare(&q2,
        "INSERT INTO ai_vector(source_type, source_id, dim, vector)"
        " VALUES('note', %d, %d, :vec)",
        nid, (int)(blob_size(&v)/sizeof(float))
      );
      db_bind_blob(&q2, ":vec", &v);
      db_step(&q2);
      db_finalize(&q2);
      n++;
    }
    blob_reset(&v);
    blob_reset(&text);
  }
  db_finalize(&q1);
  fossil_print("Indexed %d notes.\n", n);
}

/*
** Add a new note to the AI knowledge base.
*/
static void agent_note_cmd(void){
  const char *zTitle;
  const char *zTier;
  const char *zSourceType;
  const char *zSourceRef;
  const char *zProcessLevel;
  const char *zMetadata;
  const char *zArtifactKind;
  const char *zArtifactRef;
  const char *zArtifactRid;
  const char *zArtifactPath;
  const char *zArtifactStatus;
  Blob body = BLOB_INITIALIZER;
  int artifactRid = 0;
  int tier = 1;

  zTitle = find_option("title", 0, 1);
  zTier = find_option("tier", 0, 1);
  zSourceType = find_option("source-type", 0, 1);
  zSourceRef = find_option("source-ref", 0, 1);
  zProcessLevel = find_option("process-level", 0, 1);
  zMetadata = find_option("metadata", 0, 1);
  zArtifactKind = find_option("artifact-kind", 0, 1);
  zArtifactRef = find_option("artifact-ref", 0, 1);
  zArtifactRid = find_option("artifact-rid", 0, 1);
  zArtifactPath = find_option("artifact-path", 0, 1);
  zArtifactStatus = find_option("artifact-status", 0, 1);
  if( zArtifactRid ) artifactRid = atoi(zArtifactRid);
  if( zTier ){
    tier = atoi(zTier);
  }else if( find_option("tier-2", 0, 0) ){
    tier = 2;
  }
  verify_all_options();
  if( tier<0 || tier>3 ){
    fossil_fatal("tier must be between 0 and 3");
  }
  if( g.argc==4 ){
    const char *zFile = g.argv[3];
    if( file_size(zFile, ExtFILE)>=0 ){
      agent_read_body(&body, 1, zFile);
    }else{
      blob_append(&body, zFile, -1);
    }
  }else if( g.argc==3 ){
    agent_read_body(&body, 0, 0);
  }else{
    usage("note ?TEXT|FILE? [--title TEXT] [--tier N] [--tier-2]"
          " [--source-type TYPE] [--source-ref REF]"
          " [--process-level LEVEL] [--metadata JSON]"
          " [--artifact-kind KIND] [--artifact-ref REF]"
          " [--artifact-rid N] [--artifact-path PATH]"
          " [--artifact-status STATUS]");
  }
  if( !zArtifactStatus && ((zArtifactKind && zArtifactKind[0])
   || (zArtifactRef && zArtifactRef[0]) || artifactRid>0
   || (zArtifactPath && zArtifactPath[0])) ){
    zArtifactStatus = "materialized";
  }
  ai_require_enabled();
  ai_note_create(
    tier, zTitle, &body, zSourceType, 0, zSourceRef, zProcessLevel, zMetadata,
    zArtifactKind, zArtifactRef, artifactRid, zArtifactPath, zArtifactStatus
  );
  fossil_print("Added note%s%s\n",
               zTitle ? ": " : "",
               zTitle ? zTitle : "");
  blob_reset(&body);
}

/*
** Retrieve weighted note matches for QUERY and print them.
*/
static void agent_retrieve_cmd(void){
  const char *zModel = agent_embedding_model();
  const char *zLimit = find_option("limit", "n", 1);
  int nLimit = zLimit ? atoi(zLimit) : 5;
  Blob out = BLOB_INITIALIZER;

  verify_all_options();
  if( g.argc!=4 ){
    usage("retrieve QUERY [--limit N]");
  }
  if( nLimit<=0 ) nLimit = 5;
  ai_require_enabled();
  if( agent_semantic_search(zModel, g.argv[3], nLimit, &out, 1, 0)==0 ){
    fossil_print("No notes matched.\n");
  }else{
    fossil_print("%s", blob_str(&out));
  }
  blob_reset(&out);
}

/*
** Print a compact report of recorded chat-evaluation rows.
*/
static void agent_eval_report_cmd(void){
  Stmt q;
  ai_require_enabled();
  if( !db_table_exists("repository","ai_chat_eval") ){
    return;
  }
  db_prepare(&q,
    "SELECT coalesce(nullif(provider,''),'(unset)'),"
    "       coalesce(nullif(model,''),'(unset)'),"
    "       coalesce(nullif(reply_kind,''),'(unset)'),"
    "       coalesce(nullif(quality_status,''),'(unset)'),"
    "       coalesce(nullif(reasoning_status,''),'(unset)'),"
    "       coalesce(nullif(user_feedback,''),'(none)'),"
    "       count(*)"
    "  FROM ai_chat_eval"
    " GROUP BY provider, model, reply_kind, quality_status,"
    "          reasoning_status, user_feedback"
    " ORDER BY provider, model, reply_kind, quality_status,"
    "          reasoning_status, user_feedback"
  );
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("%s|%s|%s|%s|%s|%s|%d\n",
      db_column_text(&q, 0),
      db_column_text(&q, 1),
      db_column_text(&q, 2),
      db_column_text(&q, 3),
      db_column_text(&q, 4),
      db_column_text(&q, 5),
      db_column_int(&q, 6)
    );
  }
  db_finalize(&q);
}

/*
** Append a JSON array summarizing built-in recipes and whether their phase and
** capability declarations validate against the current registries.
*/
static void agent_append_recipe_registry_json(Blob *pOut){
  unsigned int i;
  blob_append(pOut, "[", 1);
  for(i=0; i<count(aAgentRecipeBuiltin); i++){
    const AgentRecipe *p = &aAgentRecipeBuiltin[i];
    const AgentPhase *pPrimary = agent_recipe_primary_phase(p);
    Blob err = BLOB_INITIALIZER;
    int ok = agent_recipe_phases_valid(p->zPhases, &err);
    if( ok ){
      blob_reset(&err);
      ok = agent_recipe_capabilities_valid(p->zCapabilities, &err);
    }
    blob_appendf(pOut,
      "%s{\"name\":%!j,\"title\":%!j,\"phase_count\":%d,"
      "\"capability_count\":%d,\"primary_phase\":%!j,"
      "\"valid\":%d,\"error\":%!j}",
      i ? "," : "",
      p->zName, p->zTitle,
      agent_list_count(p->zPhases),
      agent_list_count(p->zCapabilities),
      pPrimary ? pPrimary->zName : "",
      ok,
      ok ? "" : blob_str(&err)
    );
    blob_reset(&err);
  }
  blob_append(pOut, "]", 1);
}

/*
** Append a JSON array summarizing recent chat sessions for zUser.
*/
static void agent_append_recent_sessions_json(
  Blob *pOut,
  const char *zUser,
  int nLimit
){
  Stmt q;
  int first = 1;
  blob_append(pOut, "[", 1);
  if( nLimit>0 && db_table_exists("repository","agentchat_session") ){
    db_prepare(&q,
      "SELECT s.sid,"
      "       coalesce(nullif(s.title,''),'New Chat'),"
      "       coalesce(nullif(s.provider,''),'?'),"
      "       coalesce(nullif(s.model,''),''),"
      "       (SELECT count(*) FROM agentchat AS c WHERE c.sid=s.sid),"
      "       coalesce((SELECT kind FROM agentchat AS c"
      "                  WHERE c.sid=s.sid ORDER BY acid DESC LIMIT 1),'')"
      "  FROM agentchat_session AS s"
      " WHERE s.xfrom=%Q OR (%Q='' AND s.xfrom='')"
      " ORDER BY s.mtime DESC, s.sid DESC LIMIT %d",
      zUser ? zUser : "", zUser ? zUser : "", nLimit
    );
    while( db_step(&q)==SQLITE_ROW ){
      int sid = db_column_int(&q, 0);
      blob_appendf(pOut,
        "%s{\"sid\":%d,\"title\":%!j,\"provider\":%!j,\"model\":%!j,"
        "\"message_count\":%d,\"state\":%!j}",
        first ? "" : ",",
        sid,
        db_column_text(&q, 1),
        db_column_text(&q, 2),
        db_column_text(&q, 3),
        db_column_int(&q, 4),
        agent_chat_session_state(sid)
      );
      first = 0;
    }
    db_finalize(&q);
  }
  blob_append(pOut, "]", 1);
}

/*
** Append a JSON array summarizing grouped evaluation rows, limited to nLimit
** groups in stable order.
*/
static void agent_append_eval_summary_json(Blob *pOut, int nLimit){
  Stmt q;
  int first = 1;
  blob_append(pOut, "[", 1);
  if( nLimit>0 && db_table_exists("repository","ai_chat_eval") ){
    db_prepare(&q,
      "SELECT coalesce(nullif(provider,''),'(unset)'),"
      "       coalesce(nullif(model,''),'(unset)'),"
      "       coalesce(nullif(reply_kind,''),'(unset)'),"
      "       coalesce(nullif(quality_status,''),'(unset)'),"
      "       coalesce(nullif(reasoning_status,''),'(unset)'),"
      "       coalesce(nullif(user_feedback,''),'(none)'),"
      "       count(*)"
      "  FROM ai_chat_eval"
      " GROUP BY provider, model, reply_kind, quality_status,"
      "          reasoning_status, user_feedback"
      " ORDER BY count(*) DESC, provider, model, reply_kind, quality_status,"
      "          reasoning_status, user_feedback"
      " LIMIT %d",
      nLimit
    );
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(pOut,
        "%s{\"provider\":%!j,\"model\":%!j,\"reply_kind\":%!j,"
        "\"quality_status\":%!j,\"reasoning_status\":%!j,"
        "\"user_feedback\":%!j,\"count\":%d}",
        first ? "" : "",
        db_column_text(&q, 0),
        db_column_text(&q, 1),
        db_column_text(&q, 2),
        db_column_text(&q, 3),
        db_column_text(&q, 4),
        db_column_text(&q, 5),
        db_column_int(&q, 6)
      );
      first = 0;
    }
    db_finalize(&q);
  }
  blob_append(pOut, "]", 1);
}

/*
** CLI command: fossil agent diagnostics [--json] [--limit N]
**
** Print a compact diagnostics bundle for the effective agent configuration,
** recent runtime state, recipe registry sanity, and evaluation summaries.
*/
static void agent_diagnostics_cmd(void){
  const char *zSource = 0;
  char *zChatProvider = mprintf("%s", agent_chat_provider());
  char *zChatModel = mprintf("%s", agent_default_model());
  char *zChatCmd = mprintf("%s", agent_command_template());
  char *zEmbedProvider = mprintf("%s", agent_embedding_provider());
  char *zEmbedModel = mprintf("%s", agent_embedding_model());
  char *zEmbedCmd = mprintf("%s", agent_embedding_template());
  char *zChatModelError = 0;
  char *zChatCommandPath = 0;
  char *zChatCommandDetail = 0;
  char *zEmbedModelError = 0;
  char *zEmbedCommandPath = 0;
  char *zEmbedCommandDetail = 0;
  Blob json = BLOB_INITIALIZER;
  int jsonFlag = find_option("json", 0, 0)!=0;
  int saveFlag = find_option("save", 0, 0)!=0;
  const char *zLimit = find_option("limit", 0, 1);
  int nLimit = zLimit ? atoi(zLimit) : 5;
  int chatProviderKnown = agent_provider_is_known(zChatProvider);
  int embeddingProviderKnown = agent_provider_is_known(zEmbedProvider);
  int chatModelValid = agent_validate_provider_model_ex(
    zChatProvider, zChatModel, &zChatModelError
  );
  int chatCommandReady = agent_command_is_ready(
    zChatCmd, &zChatCommandPath, &zChatCommandDetail
  );
  int embeddingAvailable = agent_embedding_is_available();
  int embeddingModelValid = agent_validate_provider_model_ex(
    zEmbedProvider, zEmbedModel, &zEmbedModelError
  );
  int embeddingCommandReady = agent_embedding_backend_ready(
    zEmbedProvider, zEmbedModel, zEmbedCmd,
    &zEmbedCommandPath, &zEmbedCommandDetail
  );
  int chatOk = chatProviderKnown && chatModelValid && chatCommandReady;
  int embeddingOk = embeddingProviderKnown && embeddingModelValid
                 && embeddingAvailable && embeddingCommandReady;
  int overallOk = chatOk && embeddingOk;
  int noteCount;
  int vectorCount;
  int sessionCount;
  int messageCount;
  int evalCount;
  int latestSid;
  unsigned int i;
  int nRecipe = (int)count(aAgentRecipeBuiltin);
  int nInvalidRecipe = 0;
  const char *zUser = g.zLogin ? g.zLogin : "";

  verify_all_options();
  if( nLimit<=0 ) nLimit = 5;
  zSource = agent_config_source();
  noteCount = db_table_exists("repository","ai_note")
    ? db_int(0, "SELECT count(*) FROM ai_note") : 0;
  vectorCount = db_table_exists("repository","ai_vector")
    ? db_int(0, "SELECT count(*) FROM ai_vector") : 0;
  sessionCount = db_table_exists("repository","agentchat_session")
    ? db_int(0, "SELECT count(*) FROM agentchat_session") : 0;
  messageCount = db_table_exists("repository","agentchat")
    ? db_int(0, "SELECT count(*) FROM agentchat") : 0;
  evalCount = db_table_exists("repository","ai_chat_eval")
    ? db_int(0, "SELECT count(*) FROM ai_chat_eval") : 0;
  latestSid = agent_chat_latest_session(zUser);
  for(i=0; i<count(aAgentRecipeBuiltin); i++){
    Blob err = BLOB_INITIALIZER;
    if( !agent_recipe_phases_valid(aAgentRecipeBuiltin[i].zPhases, &err)
     || !agent_recipe_capabilities_valid(aAgentRecipeBuiltin[i].zCapabilities, &err) ){
      nInvalidRecipe++;
    }
    blob_reset(&err);
  }

  if( jsonFlag ){
    int runid = 0;
    blob_appendf(&json, "{\"source\":%!j,", zSource);
    blob_appendf(&json, "\"platform\":{\"manifest_version\":%!j},", MANIFEST_VERSION);
    blob_appendf(&json, "\"config\":{\"chat_provider\":%!j,\"chat_model\":%!j,"
                 "\"chat_command_path\":%!j,\"chat_command_detail\":%!j,"
                 "\"embedding_provider\":%!j,\"embedding_model\":%!j,"
                 "\"embedding_command_path\":%!j,\"embedding_command_detail\":%!j},",
       zChatProvider, zChatModel,
       zChatCommandPath ? zChatCommandPath : "",
       zChatCommandDetail ? zChatCommandDetail : "",
       zEmbedProvider, zEmbedModel,
       zEmbedCommandPath ? zEmbedCommandPath : "",
       zEmbedCommandDetail ? zEmbedCommandDetail : "");
    blob_appendf(&json, "\"verification\":{\"chat_ok\":%d,\"chat_provider_known\":%d,"
                 "\"chat_model_valid\":%d,\"chat_model_error\":%!j,"
                 "\"embedding_ok\":%d,\"embedding_provider_known\":%d,"
                 "\"embedding_model_valid\":%d,\"embedding_model_error\":%!j,"
                 "\"overall_ok\":%d},",
       chatOk, chatProviderKnown, chatModelValid,
       zChatModelError ? zChatModelError : "",
       embeddingOk, embeddingProviderKnown, embeddingModelValid,
       zEmbedModelError ? zEmbedModelError : "",
       overallOk);
    blob_appendf(&json, "\"runtime\":{\"note_count\":%d,\"vector_count\":%d,"
                 "\"session_count\":%d,\"message_count\":%d,"
                 "\"eval_count\":%d,\"latest_session\":%d},",
       noteCount, vectorCount, sessionCount, messageCount, evalCount, latestSid);
    blob_appendf(&json, "\"recipes\":{\"count\":%d,\"invalid_count\":%d,\"items\":",
       nRecipe, nInvalidRecipe);
    agent_append_recipe_registry_json(&json);
    blob_append(&json, "},\"recent_sessions\":", -1);
    agent_append_recent_sessions_json(&json, zUser, nLimit);
    blob_append(&json, ",\"eval_summary\":", -1);
    agent_append_eval_summary_json(&json, nLimit);
    blob_append(&json, "}", 1);
    if( saveFlag ){
      runid = agent_run_record(
        "diagnostics", "diagnostics",
        overallOk ? "ok" : "not-ok",
        overallOk ? "diagnostics overall ok" : "diagnostics reported failures",
        blob_str(&json)
      );
      fossil_print("{\"saved_run_id\":%d,\"diagnostics\":%s}\n",
        runid, blob_str(&json)
      );
    }else{
      fossil_print("%s\n", blob_str(&json));
    }
  }else{
    int runid = 0;
    fossil_print("source: %s\n", zSource);
    fossil_print("verification: overall=%s chat=%s embedding=%s\n",
      overallOk ? "ok" : "not-ok",
      chatOk ? "ok" : "not-ok",
      embeddingOk ? "ok" : "not-ok");
    fossil_print("chat: provider=%s model=%s command=%s\n",
      zChatProvider, zChatModel,
      zChatCommandPath ? zChatCommandPath : "(not ready)");
    fossil_print("embedding: provider=%s model=%s command=%s\n",
      zEmbedProvider, zEmbedModel,
      zEmbedCommandPath ? zEmbedCommandPath : "(not ready)");
    fossil_print("runtime: notes=%d vectors=%d sessions=%d messages=%d evals=%d latest-session=%d\n",
      noteCount, vectorCount, sessionCount, messageCount, evalCount, latestSid);
    fossil_print("recipes: count=%d invalid=%d\n", nRecipe, nInvalidRecipe);
    for(i=0; i<count(aAgentRecipeBuiltin); i++){
      const AgentRecipe *p = &aAgentRecipeBuiltin[i];
      const AgentPhase *pPrimary = agent_recipe_primary_phase(p);
      fossil_print("recipe %s: phases=%d capabilities=%d primary=%s\n",
        p->zName,
        agent_list_count(p->zPhases),
        agent_list_count(p->zCapabilities),
        pPrimary ? pPrimary->zName : "(none)");
    }
    if( sessionCount>0 ){
      Stmt q;
      fossil_print("recent sessions:\n");
      db_prepare(&q,
        "SELECT s.sid,"
        "       coalesce(nullif(s.title,''),'New Chat'),"
        "       coalesce(nullif(s.provider,''),'?'),"
        "       coalesce(nullif(s.model,''),''),"
        "       (SELECT count(*) FROM agentchat AS c WHERE c.sid=s.sid)"
        "  FROM agentchat_session AS s"
        " WHERE s.xfrom=%Q OR (%Q='' AND s.xfrom='')"
        " ORDER BY s.mtime DESC, s.sid DESC LIMIT %d",
        zUser, zUser, nLimit
      );
      while( db_step(&q)==SQLITE_ROW ){
        int sid = db_column_int(&q, 0);
        fossil_print("  %d|%s|%s|%s|messages=%d|state=%s\n",
          sid,
          db_column_text(&q, 1),
          db_column_text(&q, 2),
          db_column_text(&q, 3),
          db_column_int(&q, 4),
          agent_chat_session_state(sid)
        );
      }
      db_finalize(&q);
    }
    if( evalCount>0 ){
      Stmt q;
      fossil_print("eval summary:\n");
      db_prepare(&q,
        "SELECT coalesce(nullif(provider,''),'(unset)'),"
        "       coalesce(nullif(model,''),'(unset)'),"
        "       coalesce(nullif(reply_kind,''),'(unset)'),"
        "       coalesce(nullif(quality_status,''),'(unset)'),"
        "       coalesce(nullif(reasoning_status,''),'(unset)'),"
        "       coalesce(nullif(user_feedback,''),'(none)'),"
        "       count(*)"
        "  FROM ai_chat_eval"
        " GROUP BY provider, model, reply_kind, quality_status,"
        "          reasoning_status, user_feedback"
        " ORDER BY count(*) DESC, provider, model, reply_kind, quality_status,"
        "          reasoning_status, user_feedback"
        " LIMIT %d",
        nLimit
      );
      while( db_step(&q)==SQLITE_ROW ){
        fossil_print("  %s|%s|%s|%s|%s|%s|%d\n",
          db_column_text(&q, 0),
          db_column_text(&q, 1),
          db_column_text(&q, 2),
          db_column_text(&q, 3),
          db_column_text(&q, 4),
          db_column_text(&q, 5),
          db_column_int(&q, 6)
        );
      }
      db_finalize(&q);
    }
    if( saveFlag ){
      blob_appendf(&json, "{\"source\":%!j,", zSource);
      blob_appendf(&json, "\"platform\":{\"manifest_version\":%!j},", MANIFEST_VERSION);
      blob_appendf(&json, "\"verification\":{\"chat_ok\":%d,\"embedding_ok\":%d,"
                        "\"overall_ok\":%d},",
        chatOk, embeddingOk, overallOk
      );
      blob_appendf(&json, "\"runtime\":{\"note_count\":%d,\"vector_count\":%d,"
                        "\"session_count\":%d,\"message_count\":%d,"
                        "\"eval_count\":%d,\"latest_session\":%d}}",
        noteCount, vectorCount, sessionCount, messageCount, evalCount, latestSid
      );
      runid = agent_run_record(
        "diagnostics", "diagnostics",
        overallOk ? "ok" : "not-ok",
        overallOk ? "diagnostics overall ok" : "diagnostics reported failures",
        blob_str(&json)
      );
      fossil_print("saved-run: %d\n", runid);
    }
  }
  blob_reset(&json);
  fossil_free((char*)zSource);
  fossil_free(zChatModelError);
  fossil_free(zChatCommandPath);
  fossil_free(zChatCommandDetail);
  fossil_free(zEmbedModelError);
  fossil_free(zEmbedCommandPath);
  fossil_free(zEmbedCommandDetail);
  fossil_free(zChatProvider);
  fossil_free(zChatModel);
  fossil_free(zChatCmd);
  fossil_free(zEmbedProvider);
  fossil_free(zEmbedModel);
  fossil_free(zEmbedCmd);
}

/*
** CLI command: fossil agent run-log [--limit N]
**
** List persisted agent runs in reverse chronological order.
*/
static void agent_run_log_cmd(void){
  Stmt q;
  const char *zLimit = find_option("limit", 0, 1);
  int nLimit = zLimit ? atoi(zLimit) : 10;
  verify_all_options();
  if( nLimit<=0 ) nLimit = 10;
  agent_run_create_tables();
  db_prepare(&q,
    "SELECT runid,"
    "       datetime(created_at,toLocal()),"
    "       coalesce(nullif(kind,''),'(unset)'),"
    "       coalesce(nullif(name,''),'(unset)'),"
    "       coalesce(nullif(status,''),'(unset)'),"
    "       coalesce(nullif(summary,''),'')"
    "  FROM agentrun"
    " ORDER BY runid DESC LIMIT %d",
    nLimit
  );
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("%d|%s|%s|%s|%s|%s\n",
      db_column_int(&q, 0),
      db_column_text(&q, 1),
      db_column_text(&q, 2),
      db_column_text(&q, 3),
      db_column_text(&q, 4),
      db_column_text(&q, 5)
    );
  }
  db_finalize(&q);
}

/*
** CLI command: fossil agent run-show ID
**
** Show a persisted agent run payload and metadata.
*/
static void agent_run_show_cmd(void){
  Stmt q;
  int runid;
  if( g.argc!=4 ){
    usage("run-show ID");
  }
  runid = atoi(g.argv[3]);
  agent_run_create_tables();
  db_prepare(&q,
    "SELECT datetime(created_at,toLocal()),"
    "       coalesce(nullif(kind,''),'(unset)'),"
    "       coalesce(nullif(name,''),'(unset)'),"
    "       coalesce(nullif(status,''),'(unset)'),"
    "       coalesce(nullif(summary,''),''),"
    "       coalesce(payload,'')"
    "  FROM agentrun WHERE runid=%d",
    runid
  );
  if( db_step(&q)!=SQLITE_ROW ){
    db_finalize(&q);
    fossil_fatal("unknown run id: %d", runid);
  }
  fossil_print("run: %d\n", runid);
  fossil_print("created: %s\n", db_column_text(&q, 0));
  fossil_print("kind: %s\n", db_column_text(&q, 1));
  fossil_print("name: %s\n", db_column_text(&q, 2));
  fossil_print("status: %s\n", db_column_text(&q, 3));
  fossil_print("summary: %s\n", db_column_text(&q, 4));
  fossil_print("payload:\n%s\n", db_column_text(&q, 5));
  db_finalize(&q);
}

/*
** CLI command: fossil agent pool-process TIER [SCRIPT]
**
** Runs the given TH1 script (or a default) to transition notes
** into the requested tier.
*/
static void agent_pool_process_cmd(void){
  int tier;
  const char *zScript;
  char *zEnvScript = 0;
  int thRc;
  if( g.argc<4 ){
    usage("pool-process TIER [TH1_SCRIPT_PATH]");
  }
  tier = atoi(g.argv[3]);
  if( tier<1 || tier>3 ){
    fossil_fatal("tier must be 1, 2, or 3");
  }
  
  if( g.argc>=5 ){
    Blob b = BLOB_INITIALIZER;
    blob_read_from_file(&b, g.argv[4], ExtFILE);
    zScript = blob_str(&b);
  }else{
    const char *zEnv = fossil_getenv("FOSSIL_AGENT_POOL_SCRIPT");
    if( zEnv && zEnv[0] ){
      Blob b = BLOB_INITIALIZER;
      blob_read_from_file(&b, zEnv, ExtFILE);
      zEnvScript = blob_str(&b);
      zScript = zEnvScript;
    }else{
      zScript = "set pending [pool_list_pending $target_tier]\n"
                "if {[llength $pending] == 0} { return }\n"
                "foreach nid $pending {\n"
                "  set body [pool_get $nid]\n"
                "  set prompt \"Synthesize and upgrade this context to tier $target_tier:\\n\\n$body\"\n"
                "  set reply [agent_run $provider $model $prompt]\n"
                "  set new_nid [pool_put $target_tier $reply]\n"
                "  pool_link $nid $new_nid \\\"derived-from\\\"\n"
                "}\n"
                "return \\\"Processed [llength $pending] items\\\"\n";
    }
  }

  ai_require_enabled();
  db_begin_write();
  Th_FossilInit(TH_INIT_DEFAULT);
  Th_StoreInt("target_tier", tier);
  Th_Store("provider", agent_chat_provider());
  Th_Store("model", agent_default_model());
  
  thRc = Th_Eval(g.interp, 0, zScript, -1);
  if( thRc==TH_ERROR ){
    int nResult = 0;
    const char *zResult = Th_GetResult(g.interp, &nResult);
    fossil_fatal("pool-process TH1 error: %.*s", nResult, zResult ? zResult : "");
  }else{
    int nResult = 0;
    const char *zResult = Th_GetResult(g.interp, &nResult);
    fossil_print("pool-process completed: %.*s\n", nResult, zResult ? zResult : "success");
  }
  db_end_transaction(0);
  fossil_free(zEnvScript);
}

/*
** CLI command: fossil agent recipe list
*/
static void agent_recipe_list_cmd(void){
  unsigned int i;
  for(i=0; i<count(aAgentRecipeBuiltin); i++){
    fossil_print("%s\t%s\n",
      aAgentRecipeBuiltin[i].zName,
      aAgentRecipeBuiltin[i].zTitle
    );
  }
}

/*
** CLI command: fossil agent recipe show NAME
*/
static void agent_recipe_show_cmd(void){
  const AgentRecipe *pRecipe;
  const AgentPhase *pPrimary;
  Blob err = BLOB_INITIALIZER;
  if( g.argc!=5 ){
    usage("recipe show NAME");
  }
  pRecipe = agent_recipe_find(g.argv[4]);
  if( pRecipe==0 ){
    fossil_fatal("unknown recipe: %s", g.argv[4]);
  }
  if( !agent_recipe_phases_valid(pRecipe->zPhases, &err) ){
    fossil_fatal("%s", blob_str(&err));
  }
  blob_reset(&err);
  if( !agent_recipe_capabilities_valid(pRecipe->zCapabilities, &err) ){
    fossil_fatal("%s", blob_str(&err));
  }
  pPrimary = agent_recipe_primary_phase(pRecipe);
  fossil_print("name: %s\n", pRecipe->zName);
  fossil_print("title: %s\n", pRecipe->zTitle);
  fossil_print("description: %s\n", pRecipe->zDescription);
  fossil_print("usage: %s\n", pRecipe->zUsage);
  fossil_print("guidance-refs: %s\n",
    pRecipe->zGuidanceRefs ? pRecipe->zGuidanceRefs : "");
  fossil_print("guidance-ref-count: %d\n",
    agent_list_count(pRecipe->zGuidanceRefs));
  fossil_print("phases: %s\n", pRecipe->zPhases);
  if( pPrimary ){
    fossil_print("primary-phase: %s\n", pPrimary->zName);
  }
  fossil_print("phase-count: %d\n", agent_list_count(pRecipe->zPhases));
  fossil_print("capabilities: %s\n", pRecipe->zCapabilities);
  fossil_print("capability-count: %d\n", agent_list_count(pRecipe->zCapabilities));
}

/*
** CLI command: fossil agent recipe run NAME ?QUERY...? [--json]
*/
static void agent_recipe_run_cmd(void){
  const AgentRecipe *pRecipe;
  const AgentPhase *pPrimary;
  const char *zProvider;
  const char *zModel;
  char *zQuery;
  Blob guidance = BLOB_INITIALIZER;
  Blob guidanceJson = BLOB_INITIALIZER;
  const char *zReply;
  int jsonFlag;
  int saveFlag;
  int nReply = 0;
  Blob err = BLOB_INITIALIZER;
  Blob json = BLOB_INITIALIZER;
  const char *zSummary = "Recipe completed successfully.";
  const char *zNext = "Review the detailed report and decide whether to run verify or continue with a more specific recipe.";

  jsonFlag = find_option("json", 0, 0)!=0;
  saveFlag = find_option("save", 0, 0)!=0;
  verify_all_options();
  if( g.argc<5 ){
    usage("recipe run NAME ?QUERY...? [--json]");
  }
  pRecipe = agent_recipe_find(g.argv[4]);
  if( pRecipe==0 ){
    fossil_fatal("unknown recipe: %s", g.argv[4]);
  }
  if( !agent_recipe_phases_valid(pRecipe->zPhases, &err) ){
    fossil_fatal("%s", blob_str(&err));
  }
  blob_reset(&err);
  if( !agent_recipe_capabilities_valid(pRecipe->zCapabilities, &err) ){
    fossil_fatal("%s", blob_str(&err));
  }
  pPrimary = agent_recipe_primary_phase(pRecipe);
  if( !agent_guidance_load(pRecipe->zGuidanceRefs, &guidance, &guidanceJson, &err) ){
    fossil_fatal("%s", blob_str(&err));
  }
  zQuery = agent_join_args(5);
  zProvider = agent_chat_provider();
  zModel = agent_default_model();
  if( agent_validate_provider_model(zProvider, zModel, &err) ){
    fossil_fatal("%s", blob_str(&err));
  }
  blob_reset(&err);
  Th_FossilInit(TH_INIT_DEFAULT);
  Th_SetVar(g.interp, "provider", 8, zProvider, (int)strlen(zProvider));
  Th_SetVar(g.interp, "model", 5, zModel, (int)strlen(zModel));
  Th_SetVar(g.interp, "query", 5, zQuery ? zQuery : "", (int)strlen(zQuery ? zQuery : ""));
  Th_SetVar(g.interp, "guidance", 8, blob_str(&guidance), blob_size(&guidance));
  if( Th_Eval(g.interp, 0, pRecipe->zScript, -1)==TH_ERROR ){
    const char *zResult = Th_GetResult(g.interp, &nReply);
    fossil_fatal("recipe TH1 error: %.*s", nReply, zResult ? zResult : "");
  }
  zReply = Th_GetResult(g.interp, &nReply);
  blob_appendf(&json, "{\"status\":\"ok\",\"recipe\":%!j,\"title\":%!j,"
               "\"provider\":%!j,\"model\":%!j,\"query\":%!j,"
               "\"guidance_ref_count\":%d,\"guidance_refs\":%s,"
               "\"phase_count\":%d,\"phases\":",
    pRecipe->zName, pRecipe->zTitle, zProvider, zModel,
    zQuery ? zQuery : "",
    agent_list_count(pRecipe->zGuidanceRefs), blob_str(&guidanceJson),
    agent_list_count(pRecipe->zPhases)
  );
  agent_append_phase_name_array(&json, pRecipe->zPhases);
  blob_append(&json, ",\"primary_phase\":", -1);
  if( pPrimary ){
    blob_appendf(&json, "{\"name\":%!j,\"title\":%!j,\"description\":%!j}",
      pPrimary->zName, pPrimary->zTitle, pPrimary->zDescription
    );
  }else{
    blob_append(&json, "null", -1);
  }
  blob_appendf(&json, ",\"capability_count\":%d,\"capabilities\":",
    agent_list_count(pRecipe->zCapabilities)
  );
  agent_append_capability_name_array(&json, pRecipe->zCapabilities);
  blob_appendf(&json, ",\"executive_summary\":%!j,"
               "\"detailed_report\":%!j,"
               "\"artifacts\":[],"
               "\"risks\":[],"
               "\"next_recommended\":%!j,"
               "\"reply\":%!j}",
    zSummary,
    zReply ? zReply : "",
    zNext,
    zReply ? zReply : ""
  );
  if( jsonFlag ){
    if( saveFlag ){
      int runid = agent_run_record(
        "recipe", pRecipe->zName, "ok",
        zSummary, blob_str(&json)
      );
      fossil_print("{\"saved_run_id\":%d,\"recipe_run\":%s}\n",
        runid, blob_str(&json)
      );
    }else{
      fossil_print("%s\n", blob_str(&json));
    }
  }else{
    fossil_print("recipe: %s\n", pRecipe->zName);
    fossil_print("provider: %s\n", zProvider);
    fossil_print("model: %s\n", zModel);
    if( pRecipe->zGuidanceRefs && pRecipe->zGuidanceRefs[0] ){
      fossil_print("guidance-refs: %s\n", pRecipe->zGuidanceRefs);
    }
    fossil_print("phases: %s\n", pRecipe->zPhases);
    if( pPrimary ){
      fossil_print("primary-phase: %s\n", pPrimary->zName);
    }
    if( zQuery && zQuery[0] ){
      fossil_print("query: %s\n", zQuery);
    }
    fossil_print("summary: %s\n", zSummary);
    fossil_print("report:\n%.*s\n", nReply, zReply ? zReply : "");
    fossil_print("next: %s\n", zNext);
    if( saveFlag ){
      int runid = agent_run_record(
        "recipe", pRecipe->zName, "ok",
        zSummary, blob_str(&json)
      );
      fossil_print("saved-run: %d\n", runid);
    }
  }
  blob_reset(&guidanceJson);
  blob_reset(&guidance);
  blob_reset(&json);
  fossil_free(zQuery);
}

/*
** Dispatch fossil agent recipe ...
*/
static void agent_recipe_cmd(void){
  const char *zSub;
  if( g.argc<4 ){
    usage("recipe SUBCOMMAND ...");
  }
  zSub = g.argv[3];
  if( fossil_strcmp(zSub, "list")==0 ){
    agent_recipe_list_cmd();
  }else if( fossil_strcmp(zSub, "show")==0 ){
    agent_recipe_show_cmd();
  }else if( fossil_strcmp(zSub, "run")==0 ){
    agent_recipe_run_cmd();
  }else{
    fossil_fatal("unknown recipe subcommand: %s", zSub);
  }
}

/*
** CLI command: fossil agent phases
*/
static void agent_phases_cmd(void){
  unsigned int i;
  for(i=0; i<count(aAgentPhaseBuiltin); i++){
    const AgentPhase *p = &aAgentPhaseBuiltin[i];
    fossil_print("%s\t%s\t%s\n", p->zName, p->zTitle, p->zDescription);
  }
}

/*
** CLI command: fossil agent capabilities
*/
static void agent_capabilities_cmd(void){
  unsigned int i;
  for(i=0; i<count(aAgentCapabilityBuiltin); i++){
    const AgentCapability *p = &aAgentCapabilityBuiltin[i];
    fossil_print("%s\t%s\twrite=%s\tnetwork=%s\tconfirm=%s\t%s\n",
      p->zName,
      p->zKind,
      p->requiresWrite ? "yes" : "no",
      p->requiresNetwork ? "yes" : "no",
      p->requiresConfirm ? "yes" : "no",
      p->zDescription
    );
  }
}

/*
** COMMAND: agent
**
** Usage: %fossil agent SUBCOMMAND ...
**
** Commands intended to help agent-style development workflows while keeping
** the integration within Fossil's existing command and wiki model.
**
** Common option:
**
**    --agent-config FILE
**       Read agent settings from FILE instead of cfg/ai-agent.json.
**
**    fossil agent repomap
**       Print the managed file list for the current checkout.
**
**    fossil agent changes
**       Print pending managed-file changes for the current checkout.
**
**    fossil agent embed TEXT
**       Generate and print (as hex) the embedding for TEXT.
**
**    fossil agent eval-report
**       Print grouped `ai_chat_eval` summary rows.
**
**    fossil agent diagnostics [--json] [--limit N]
**       Print a compact diagnostics bundle covering config, verification,
**       runtime counts, recipe registry sanity, recent sessions, and evals.
**       With --save, persist the diagnostics payload in repository storage.
**
**    fossil agent capabilities
**       List the built-in capability registry for recipe and agent use.
**
**    fossil agent phases
**       List the built-in orchestration phase registry.
**
**    fossil agent recipe list
**    fossil agent recipe show NAME
**    fossil agent recipe run NAME ?QUERY...? [--json] [--save]
**       List, inspect, and execute built-in agent recipes.
**
**    fossil agent run-log [--limit N]
**    fossil agent run-show ID
**       Inspect saved agent runs such as persisted diagnostics payloads.
**
**    fossil agent verify [--json] [--chat-smoke] [--embed-smoke] [--save]
**       Verify the effective chat and embedding configuration. Smoke flags
**       execute the configured backend(s). With --save, persist the result.
**
**    fossil agent semantic-index
**       Generate embeddings for all notes and store them in ai_vector.
**
**    fossil agent note ?FILE? [--title TEXT] [--tier N]
**                             [--source-type TYPE] [--source-ref REF]
**                             [--process-level LEVEL] [--metadata JSON]
**                             [--artifact-kind KIND] [--artifact-ref REF]
**                             [--artifact-rid N] [--artifact-path PATH]
**                             [--artifact-status STATUS]
**       Add a new note to the AI data pool.
**
**    fossil agent retrieve QUERY [--limit N]
**       Retrieve weighted note matches and reinforce them.
**
**    fossil agent wiki-sync PAGENAME ?FILE? [--append] [--dry-run]
**                             [--title TEXT] [--status TEXT]
**       Create or update PAGENAME with an agent-authored manager update.
**       The body comes from FILE or stdin.  Checkout metadata and current
**       pending changes are added as context for the human reader.
*/
void agent_cmd(void){
  const char *zCmd;
  const char *zEmbeddingModel;

  zAgentConfigPath = find_option("agent-config", 0, 1);
  find_repository_option();
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  if( g.argc<3 ){
    usage("SUBCOMMAND ...");
  }
  zEmbeddingModel = agent_embedding_model();
  zCmd = g.argv[2];
  if( fossil_strcmp(zCmd, "repomap")==0 ){
    agent_repomap_cmd();
  }else if( fossil_strcmp(zCmd, "changes")==0 ){
    agent_changes_cmd();
  }else if( fossil_strcmp(zCmd, "note")==0 ){
    agent_note_cmd();
  }else if( fossil_strcmp(zCmd, "retrieve")==0 ){
    agent_retrieve_cmd();
  }else if( fossil_strcmp(zCmd, "embed")==0 ){
    Blob v = BLOB_INITIALIZER;
    if( g.argc<4 ) usage("agent embed TEXT");
    if( agent_generate_embedding(zEmbeddingModel, g.argv[3], &v)==0 ){
      int i;
      float *af = (float*)blob_buffer(&v);
      int n = blob_size(&v) / sizeof(float);
      for(i=0; i<n; i++) fossil_print("%f%s", af[i], i<n-1?", ":"");
      fossil_print("\n");
    }else{
      fossil_fatal("failed to generate embedding");
    }
    blob_reset(&v);
  }else if( fossil_strcmp(zCmd, "diagnostics")==0 ){
    agent_diagnostics_cmd();
  }else if( fossil_strcmp(zCmd, "run-log")==0 ){
    agent_run_log_cmd();
  }else if( fossil_strcmp(zCmd, "run-show")==0 ){
    agent_run_show_cmd();
  }else if( fossil_strcmp(zCmd, "eval-report")==0 ){
    agent_eval_report_cmd();
  }else if( fossil_strcmp(zCmd, "phases")==0 ){
    agent_phases_cmd();
  }else if( fossil_strcmp(zCmd, "capabilities")==0 ){
    agent_capabilities_cmd();
  }else if( fossil_strcmp(zCmd, "recipe")==0 ){
    agent_recipe_cmd();
  }else if( fossil_strcmp(zCmd, "verify")==0 ){
    agent_verify_cmd();
  }else if( fossil_strcmp(zCmd, "semantic-index")==0 ){
    agent_semantic_index_cmd();
  }else if( fossil_strcmp(zCmd, "wiki-sync")==0 ){
    agent_wiki_sync_cmd();
  }else if( fossil_strcmp(zCmd, "pool-process")==0 ){
    agent_pool_process_cmd();
  }else if( fossil_strcmp(zCmd, "mcp")==0 ){
    void agent_mcp_cmd(void);
    agent_mcp_cmd();
  }else{
    fossil_fatal("unknown agent subcommand: %s", zCmd);
  }
}

/*
** WEBPAGE: agentui
**
** Knowledge console for interactive orchestration and retrieval tracing.
*/
void agentui_page(void){
  int sidCurrent;
  int sidRequested;
  const char *zUser;
  char *zProvider;
  char *zModel;
  char *zEmbedProvider;
  char *zEmbedCmd;
  char *zEmbedModel;
  char *zConfigSource;
  char *zCmd;
  int chatProviderLocked;

  login_check_credentials();
  if( !g.perm.Read ){
    login_needed(g.anon.Read);
    return;
  }
  zUser = (g.zLogin && g.zLogin[0]) ? g.zLogin : "guest";
  sidRequested = atoi(PD("sid","0"));
  sidCurrent = agent_chat_session_exists(sidRequested) ? sidRequested : 0;

  zProvider = mprintf("%s", agent_chat_session_provider(sidCurrent, agent_chat_provider()));
  zModel = mprintf("%s", agent_chat_session_model(sidCurrent, agent_default_model()));
  zCmd = mprintf("%s", agent_command_template());
  zEmbedProvider = mprintf("%s", agent_embedding_provider());
  zEmbedCmd = mprintf("%s", agent_embedding_template());
  zEmbedModel = mprintf("%s", agent_embedding_model());
  zConfigSource = agent_config_source();
  chatProviderLocked = agent_chat_provider_locked();

  style_set_current_feature("agent");
  agent_console_submenu(sidCurrent);
  style_header("AI Agent");
  
  /* Set up dynamic variables for TH1 */
  Th_FossilInit(TH_INIT_DEFAULT);
  Th_StoreInt("sid", sidCurrent);
  Th_SetVar(g.interp, "user", 4, zUser, -1);
  Th_SetVar(g.interp, "style_nonce", 11, style_nonce(), -1);
  Th_SetVar(g.interp, "repo_url", 8, g.zTop, -1);
  Th_SetVar(g.interp, "chat_provider", 13, zProvider, -1);
  Th_SetVar(g.interp, "chat_model", 10, zModel ? zModel : "", -1);
  Th_SetVar(g.interp, "embed_provider", 14, zEmbedProvider, -1);
  Th_SetVar(g.interp, "embed_model", 11, zEmbedModel ? zEmbedModel : "", -1);
  Th_SetVar(g.interp, "config_source", 13, zConfigSource, -1);
  Th_SetVar(g.interp, "capabilities", 28, "chat-stream,context,roles", -1);
  Th_SetVar(g.interp, "provider_disabled_attr", 22, chatProviderLocked ? "disabled" : "", -1);
  
  /* Render history into a variable */
  {
    Blob history = BLOB_INITIALIZER;
    agent_chat_render_history_to_blob(sidCurrent, &history);
    Th_SetVar(g.interp, "history_html", 12, blob_str(&history), blob_size(&history));
    blob_reset(&history);
  }

  /* Render sessions into a variable */
  {
    Blob sessions = BLOB_INITIALIZER;
    agent_chat_render_sessions_to_blob(zUser, sidCurrent, &sessions);
    Th_SetVar(g.interp, "sessions_html", 13, blob_str(&sessions), blob_size(&sessions));
    blob_reset(&sessions);
  }

  /* Load and render the template and resources */
  {
    Blob template = BLOB_INITIALIZER;
    Blob css = BLOB_INITIALIZER;
    Blob js = BLOB_INITIALIZER;
    char *zPath;
    
    zPath = mprintf("%scfg/agentui.css", g.zLocalRoot);
    if( blob_read_from_file(&css, zPath, ExtFILE)>=0 ){
      Th_SetVar(g.interp, "ui_css", 6, blob_str(&css), blob_size(&css));
    }
    fossil_free(zPath);

    zPath = mprintf("%scfg/agentui.js", g.zLocalRoot);
    if( blob_read_from_file(&js, zPath, ExtFILE)>=0 ){
      Th_SetVar(g.interp, "ui_js", 5, blob_str(&js), blob_size(&js));
    }
    fossil_free(zPath);

    zPath = mprintf("%scfg/agentui.th1", g.zLocalRoot);
    if( blob_read_from_file(&template, zPath, ExtFILE)>=0 ){
      Th_Render(blob_str(&template));
    }else{
      CX("<p class=\"error\">Error: cfg/agentui.th1 not found at %h</p>", zPath);
    }
    fossil_free(zPath);
    blob_reset(&template);
    blob_reset(&css);
    blob_reset(&js);
  }

  fossil_free(zModel);
  fossil_free(zCmd);
  fossil_free(zEmbedModel);
  fossil_free(zEmbedCmd);
  fossil_free(zProvider);
  fossil_free(zEmbedProvider);
  fossil_free(zConfigSource);
  style_finish_page();
}

/*
** WEBPAGE: agent-config
**
** JSON description of the effective chat and embedding configuration used by
** /agentui. Optional query parameter: sid.
*/
void agent_config_page(void){
  int sidRequested;
  int sidCurrent;

  login_check_credentials();
  if( !g.perm.Read ){
    cgi_set_content_type("application/json");
    CX("{\"error\":%!j}\n", "missing read permissions or not logged in");
    return;
  }
  sidRequested = atoi(PD("sid","0"));
  sidCurrent = agent_chat_session_exists(sidRequested) ? sidRequested : 0;
  cgi_set_content_type("application/json");
  agent_emit_config_json(sidCurrent);
}

/*
** WEBPAGE: agent-history
**
** JSON description of a stored chat session and its ordered messages.
** Query parameter: sid.
*/
void agent_history_page(void){
  int sidRequested;
  int sidCurrent;

  login_check_credentials();
  if( !g.perm.Read ){
    cgi_set_content_type("application/json");
    CX("{\"error\":%!j}\n", "missing read permissions or not logged in");
    return;
  }
  sidRequested = atoi(PD("sid","0"));
  sidCurrent = agent_chat_session_exists(sidRequested) ? sidRequested : 0;
  cgi_set_content_type("application/json");
  agent_emit_history_json(sidCurrent);
}

/*
** WEBPAGE: agent-events
**
** JSON description of ordered stored chat events for a session.
** Query parameters:
**
**    sid=SID      Session id
**    after=ACID   Optional lower bound for incremental polling
*/
void agent_events_page(void){
  int sidRequested;
  int sidCurrent;
  int afterAcid;

  login_check_credentials();
  if( !g.perm.Read ){
    cgi_set_content_type("application/json");
    CX("{\"error\":%!j}\n", "missing read permissions or not logged in");
    return;
  }
  sidRequested = atoi(PD("sid","0"));
  sidCurrent = agent_chat_session_exists(sidRequested) ? sidRequested : 0;
  afterAcid = atoi(PD("after","0"));
  if( afterAcid<0 ) afterAcid = 0;
  cgi_set_content_type("application/json");
  agent_emit_events_json(sidCurrent, afterAcid);
}

/*
** WEBPAGE: agent-pool
**
** JSON summary of the current note pool grouped by processing tier.
*/
void agent_pool_page(void){
  login_check_credentials();
  if( !g.perm.Read ){
    cgi_set_content_type("application/json");
    CX("{\"error\":%!j}\n", "missing read permissions or not logged in");
    return;
  }
  cgi_set_content_type("application/json");
  agent_emit_pool_json();
}

/*
** WEBPAGE: agent-retrieval
**
** JSON description of one retrieval run and its retrieved notes.
**
** Query parameters:
**
**    qid=QID
*/
void agent_retrieval_page(void){
  int qid;
  login_check_credentials();
  if( !g.perm.Read ){
    cgi_set_content_type("application/json");
    CX("{\"error\":%!j}\n", "missing read permissions or not logged in");
    return;
  }
  qid = atoi(PD("qid","0"));
  cgi_set_content_type("application/json");
  agent_emit_retrieval_json(qid);
}

/*
** WEBPAGE: agent-feedback
**
** JSON endpoint to record user feedback for the latest terminal agent reply
** in a session, or for a specific acid if provided.
**
** Parameters:
**
**    sid=SID
**    acid=ACID      Optional target reply/error row
**    feedback=TEXT  One of: useful, not-useful
*/
void agent_feedback_page(void){
  int sid;
  int acid;
  const char *zFeedback;

  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    CX("{\"error\":%!j}\n", "missing read permissions or not logged in");
    return;
  }
  sid = atoi(PD("sid","0"));
  if( !agent_chat_session_exists(sid) ){
    CX("{\"error\":%!j}\n", "missing or unknown sid parameter");
    return;
  }
  acid = atoi(PD("acid","0"));
  if( acid<=0 ) acid = agent_chat_latest_terminal_acid(sid);
  if( !agent_chat_is_terminal_acid(sid, acid) ){
    CX("{\"error\":%!j}\n", "missing or invalid terminal reply target");
    return;
  }
  if( !db_table_exists("repository","ai_chat_eval")
   || !db_exists("SELECT 1 FROM ai_chat_eval WHERE sid=%d AND acid=%d", sid, acid) ){
    CX("{\"error\":%!j}\n", "no evaluation row found for reply target");
    return;
  }
  zFeedback = PD("feedback","");
  if( fossil_strcmp(zFeedback, "useful")!=0
   && fossil_strcmp(zFeedback, "not-useful")!=0 ){
    CX("{\"error\":%!j}\n", "feedback must be 'useful' or 'not-useful'");
    return;
  }
  db_begin_write();
  db_unprotect(PROTECT_READONLY);
  ai_chat_eval_feedback(sid, acid, zFeedback);
  db_end_transaction(0);
  CX("{\"sid\":%d,\"acid\":%d,\"feedback\":%!j}\n", sid, acid, zFeedback);
}

/*
** Builtin TH1 orchestration script for the AI agent.
*/
static const char zAgentOrchestrateBuiltin[] =
"set thinking_tag [agent_config thinking_tag]\n"
"if {[string compare $thinking_tag \"\"] == 0} {set thinking_tag \"thought\"}\n"
"\n"
"set full_prompt $msg\n"
"set context \"\"\n"
"set retrieval_qid 0\n"
"\n"
"if {$context_enabled} {\n"
"  set event_meta \"{\\\"stage\\\":\\\"context\\\",\\\"status\\\":\\\"running\\\"}\"\n"
"  set event_msg \"Assembling repository context...\"\n"
"  agent_save_event $sid $user \"progress\" $provider $model $event_meta $event_msg\n"
"  set context [agent_context $msg $model]\n"
"  set retrieval_qid [agent_last_retrieval_qid]\n"
"  if {[string compare $context \"\"] != 0} {\n"
"    if {$retrieval_qid > 0} {\n"
"      set event_meta \"{\\\"stage\\\":\\\"context\\\",\\\"hidden\\\":true,\\\"retrieval_qid\\\":$retrieval_qid}\"\n"
"    } else {\n"
"      set event_meta \"{\\\"stage\\\":\\\"context\\\",\\\"hidden\\\":true}\"\n"
"    }\n"
"    agent_save_event $sid $user \"context\" $provider $model $event_meta $context\n"
"    set full_prompt \"Context:\\n$context\\n\\nUser request:\\n$msg\"\n"
"  }\n"
"  set event_meta \"{\\\"stage\\\":\\\"context\\\",\\\"status\\\":\\\"ok\\\"}\"\n"
"  set event_msg \"Repository context assembled\"\n"
"  agent_save_event $sid $user \"progress\" $provider $model $event_meta $event_msg\n"
"}\n"
"\n"
"set event_meta \"{\\\"tool\\\":\\\"chat-backend\\\",\\\"provider\\\":\\\"$provider\\\"}\"\n"
"set event_msg \"Invoking $provider backend\"\n"
"agent_save_event $sid $user \"tool\" $provider $model $event_meta $event_msg\n"
"set event_meta \"{\\\"stage\\\":\\\"backend\\\",\\\"status\\\":\\\"running\\\"}\"\n"
"set event_msg \"Waiting for backend reply...\"\n"
"agent_save_event $sid $user \"progress\" $provider $model $event_meta $event_msg\n"
"\n"
"if {[string compare $context \"\"] != 0} {\n"
"  if {$retrieval_qid > 0} {\n"
"    set prompt_meta \"{\\\"context\\\":true,\\\"retrieval_qid\\\":$retrieval_qid}\"\n"
"  } else {\n"
"    set prompt_meta \"{\\\"context\\\":true}\"\n"
"  }\n"
"} else {\n"
"  set prompt_meta \"{\\\"context\\\":false}\"\n"
"}\n"
"agent_save $sid $user \"user\" \"prompt\" $provider $model $prompt_meta $msg\n"
"\n"
"if {[catch {agent_run $provider $model $full_prompt} reply]} {\n"
"  set event_meta \"{\\\"stage\\\":\\\"backend\\\",\\\"status\\\":\\\"error\\\"}\"\n"
"  set event_msg \"Backend reply failed\"\n"
"  agent_save_event $sid $user \"progress\" $provider $model $event_meta $event_msg\n"
"  set acid [agent_save $sid $user \"agent\" \"error\" $provider $model \"\" $reply]\n"
"  agent_eval $sid $acid $provider $model \"error\" $reply\n"
"  return \"{\\\"error\\\":[agent_json_quote $reply]}\"\n"
"}\n"
"\n"
"set start_tag \"<$thinking_tag>\"\n"
"set end_tag \"</$thinking_tag>\"\n"
"set s [string first $start_tag $reply]\n"
"set e [string first $end_tag $reply]\n"
"set thinking \"\"\n"
"set clean_reply $reply\n"
"if {$s != -1} {\n"
"  if {$e != -1} {\n"
"    set s_end [expr {$s + [string length $start_tag]}]\n"
"    set thinking [string range $reply $s_end [expr {$e - 1}]]\n"
"    if {$s == 0} {\n"
"      set pre \"\"\n"
"    } else {\n"
"      set pre [string range $reply 0 [expr {$s - 1}]]\n"
"    }\n"
"    set post [string range $reply [expr {$e + [string length $end_tag]}] [expr {[string length $reply] - 1}]]\n"
"    if {[string length $pre] == 0} {\n"
"      set clean_reply $post\n"
"    } elseif {[string length $post] == 0} {\n"
"      set clean_reply $pre\n"
"    } else {\n"
"      set clean_reply \"$pre $post\"\n"
"    }\n"
"  }\n"
"}\n"
"\n"
"set event_meta \"{\\\"stage\\\":\\\"backend\\\",\\\"status\\\":\\\"ok\\\"}\"\n"
"set event_msg \"Backend reply received\"\n"
"agent_save_event $sid $user \"progress\" $provider $model $event_meta $event_msg\n"
"\n"
"set meta \"\"\n"
"if {[string compare $thinking \"\"] != 0} {\n"
"  set meta \"{\\\"thinking\\\":[agent_json_quote $thinking]}\"\n"
"}\n"
"set acid [agent_save $sid $user \"agent\" \"reply\" $provider $model $meta $clean_reply]\n"
"agent_eval $sid $acid $provider $model \"reply\" $reply\n"
"return \"{\\\"sid\\\":$sid,\\\"provider\\\":[agent_json_quote $provider],\\\"model\\\":[agent_json_quote $model],\\\"reply\\\":[agent_json_quote $reply]}\"\n"
;

/*
** WEBPAGE: agent-chat
**
** JSON endpoint for the configured agent chat UI. Refactored to use TH1 orchestration.
*/
void agent_chat_page(void){
  Blob err = BLOB_INITIALIZER;
  const char *zMsg;
  const char *zModel;
  const char *zProvider;
  const char *zUser;
  int sid;

  login_check_credentials();
  if( !g.perm.Read ){
    cgi_set_content_type("application/json");
    CX("{\"error\":%!j}\n", "missing read permissions or not logged in");
    return;
  }
  zMsg = PD("msg", "");
  zProvider = PD("provider", agent_chat_provider());
  zModel = PD("model", agent_default_model());
  zUser = (g.zLogin && g.zLogin[0]) ? g.zLogin : "guest";
  sid = atoi(PD("sid","0"));
  cgi_set_content_type("application/json");

  if( zMsg[0]==0 || zModel[0]==0 ){
    CX("{\"error\":%!j}\n", "missing msg or model parameter");
    return;
  }
  if( agent_validate_provider_model(zProvider, zModel, &err) ){
    CX("{\"error\":%!j}\n", blob_str(&err));
    blob_reset(&err);
    return;
  }

  db_begin_write();
  db_unprotect(PROTECT_READONLY);
  if( sid<=0 || !agent_chat_session_exists(sid) ){
    sid = agent_chat_session_create(zUser, zProvider, zModel);
  }

  /* Execute TH1 orchestration */
  Th_FossilInit(TH_INIT_DEFAULT);
  Th_StoreInt("sid", sid);
  Th_SetVar(g.interp, "msg",       3, zMsg,      (int)strlen(zMsg));
  Th_SetVar(g.interp, "provider",  8, zProvider, (int)strlen(zProvider));
  Th_SetVar(g.interp, "model",     5, zModel,    (int)strlen(zModel));
  Th_SetVar(g.interp, "user",      4, zUser,     (int)strlen(zUser));
  Th_StoreInt("context_enabled", PB("context"));

  {
    int thRc = Th_Eval(g.interp, 0, zAgentOrchestrateBuiltin, -1);
    int nResult = 0;
    const char *zResult = Th_GetResult(g.interp, &nResult);
    if( thRc==TH_ERROR ){
      CX("{\"error\":%!j}\n", zResult ? zResult : "TH1 eval failed");
    }else{
      /* TH_OK or TH_RETURN are both success; script result is the JSON */
      CX("%.*s\n", nResult, zResult ? zResult : "{}");
    }
  }
  
  db_end_transaction(0);
  blob_reset(&err);
}

/*
** WEBPAGE: agent-chat-stream
**
** SSE (Server-Sent Events) endpoint for streaming agent chat.
*/
void agent_chat_stream_page(void){
  const char *zMsg;
  const char *zModel;
  const char *zProvider;
  const char *zUser;
  int sid;

  login_check_credentials();
  if( !g.perm.Read ){
    cgi_set_content_type("text/plain");
    CX("error: missing read permissions or not logged in\n");
    return;
  }
  zMsg = PD("msg", "");
  zProvider = PD("provider", agent_chat_provider());
  zModel = PD("model", agent_default_model());
  zUser = (g.zLogin && g.zLogin[0]) ? g.zLogin : "guest";
  sid = atoi(PD("sid","0"));
  const char *zRoleParam = PD("role", "");
  
  /* Disable Fossil's standard output buffering for SSE */
  cgi_set_content_type("text/event-stream");
  cgi_printf("Cache-Control: no-cache\nConnection: keep-alive\n\n");
  fflush(stdout);

  if( zMsg[0]==0 || zModel[0]==0 ){
    if( fossil_strcmp(zRoleParam, "reviewer")!=0 ){
      CX("data: {\"error\":\"missing msg or model parameter\"}\n\n");
      return;
    }
  }

  db_begin_write();
  db_unprotect(PROTECT_READONLY);
  if( sid<=0 || !agent_chat_session_exists(sid) ){
    sid = agent_chat_session_create(zUser, zProvider, zModel);
  }

  Th_FossilInit(TH_INIT_DEFAULT);
  Th_StoreInt("sid", sid);
  Th_SetVar(g.interp, "msg",       3, zMsg,      (int)strlen(zMsg));
  Th_SetVar(g.interp, "provider",  8, zProvider, (int)strlen(zProvider));
  Th_SetVar(g.interp, "model",     5, zModel,    (int)strlen(zModel));
  Th_SetVar(g.interp, "user",      4, zUser,     (int)strlen(zUser));
  Th_StoreInt("context_enabled", PB("context"));

  /* Direct TH1 run with streaming */
  {
    Blob script = BLOB_INITIALIZER;
    char *zPath;
    if( zRoleParam[0] ){
      zPath = mprintf("%scfg/roles/%s.th1", g.zLocalRoot, zRoleParam);
    }else{
      zPath = mprintf("%scfg/roles/default.th1", g.zLocalRoot);
    }
    if( blob_read_from_file(&script, zPath, ExtFILE)>=0 ){
      Th_Eval(g.interp, 0, blob_str(&script), -1);
    }else{
      CX("data: {\"error\":\"Role script not found: %s\"}\n\n", zRoleParam[0] ? zRoleParam : "default");
    }
    fossil_free(zPath);
    blob_reset(&script);
  }
  db_end_transaction(0);
}

/*
** TH1 command: agent_json_extract JSON FIELD
**
** CRUDE: Extract a simple string value from a 'flat' JSON object for the prototype.
*/
static int agent_json_extract_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  const char *zJson = argv[1];
  const char *zField = argv[2];
  char *zKey;
  char *zStart, *zEnd;
  if( argc!=3 ) return Th_WrongNumArgs(interp, "agent_json_extract JSON FIELD");
  zKey = mprintf("\"%s\":\"", zField);
  zStart = strstr(zJson, zKey);
  if( zStart ){
    zStart += strlen(zKey);
    zEnd = strchr(zStart, '\"');
    if( zEnd ){
      Th_SetResult(interp, zStart, (int)(zEnd - zStart));
    }
  }
  fossil_free(zKey);
  return TH_OK;
}

/*
** TH1 command: agent_json_quote STRING
**
** Returns a JSON-quoted version of the string.
*/
static int agent_json_quote_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  char *zEsc;
  if( argc!=2 ) return Th_WrongNumArgs(interp, "agent_json_quote STRING");
  zEsc = mprintf("%!j", argv[1]);
  Th_SetResult(interp, zEsc, -1);
  fossil_free(zEsc);
  return TH_OK;
}

/*
** TH1 command: agent_context MESSAGE ?MODEL?
*/
static int agent_context_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Blob out = BLOB_INITIALIZER;
  int qid = 0;
  if( argc<2 || argc>3 ){
    return Th_WrongNumArgs(interp, "agent_context MESSAGE ?MODEL?");
  }
  agent_assemble_context(&out, argc==3 ? argv[2] : 0, argv[1], &qid);
  Th_SetResult(interp, blob_str(&out), blob_size(&out));
  g.ai_last_retrieval_qid = qid;
  blob_reset(&out);
  return TH_OK;
}

/*
** TH1 command: agent_last_retrieval_qid
*/
static int agent_last_retrieval_qid_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Th_SetResultInt(interp, g.ai_last_retrieval_qid);
  return TH_OK;
}

/*
** TH1 command: agent_run PROVIDER MODEL MSG
*/
static int agent_run_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Blob out = BLOB_INITIALIZER;
  int rc;
  if( argc!=4 ){
    return Th_WrongNumArgs(interp, "agent_run PROVIDER MODEL MSG");
  }
  rc = agent_run_backend_core(argv[1], argv[2], argv[3], &out, 0, 0, 0);
  Th_SetResult(interp, blob_str(&out), blob_size(&out));
  blob_reset(&out);
  return rc==0 ? TH_OK : TH_ERROR;
}

/*
** TH1 command: agent_run_stream PROVIDER MODEL MSG
*/
static int agent_run_stream_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Blob out = BLOB_INITIALIZER;
  Blob err = BLOB_INITIALIZER;
  int rc;
  if( argc!=4 ){
    return Th_WrongNumArgs(interp, "agent_run_stream PROVIDER MODEL MSG");
  }
  rc = agent_run_backend_core(
    argv[1], argv[2], argv[3], &out, &err, agent_sse_handler, 0
  );
  if( rc!=0 ){
    Th_SetResult(interp, blob_str(&err), blob_size(&err));
    rc = TH_ERROR;
  }else{
    Th_SetResult(interp, blob_str(&out), blob_size(&out));
  }
  blob_reset(&out);
  blob_reset(&err);
  return rc;
}

/*
** TH1 command: agent_mcp_call TOOL_NAME ?ARGS...?
*/
static int agent_mcp_call_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Blob out = BLOB_INITIALIZER;
  if( argc<2 ){
    return Th_WrongNumArgs(interp, "agent_mcp_call TOOL_NAME ?ARGS...?");
  }
  
  const char *zTool = argv[1];
  if( fossil_strcmp(zTool, "list_files")==0 ){
    int vid = db_lget_int("checkout", 0);
    Stmt q;
    int first = 1;
    blob_appendf(&out, "Files:\n");
    db_prepare(&q, "SELECT pathname FROM vfile WHERE vid=%d AND deleted=0 ORDER BY pathname LIMIT 50", vid);
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&out, "%s  %s", first ? "" : "\n", db_column_text(&q, 0));
      first = 0;
    }
    db_finalize(&q);
  }else if( fossil_strcmp(zTool, "read_file")==0 ){
    if( argc<3 ) return Th_WrongNumArgs(interp, "agent_mcp_call read_file PATH");
    if( blob_read_from_file(&out, argv[2], ExtFILE)<0 ){
       blob_appendf(&out, "Error: could not read file %s", argv[2]);
    }
  }else if( fossil_strcmp(zTool, "edit_file")==0 ){
    /* edit_file PATH EXPL REPLACE WITH CONFIRMED */
    if( argc<6 ) return Th_WrongNumArgs(interp, "agent_mcp_call edit_file PATH EXPL REPLACE WITH CONFIRMED");
    const char *zPath = argv[2];
    const char *zReplace = argv[4];
    const char *zWith = argv[5];
    int bConfirmed = atoi(argv[6]);

    if( bConfirmed ){
       Blob content = BLOB_INITIALIZER;
       if( blob_read_from_file(&content, zPath, ExtFILE)>=0 ){
         char *zOld = (char*)blob_str(&content);
         char *zPos = strstr(zOld, zReplace);
         if( zPos ){
           Blob next = BLOB_INITIALIZER;
           blob_append(&next, zOld, (int)(zPos - zOld));
           blob_append(&next, zWith, -1);
           blob_append(&next, zPos + strlen(zReplace), -1);
           blob_write_to_file(&next, zPath);
           blob_appendf(&out, "Successfully applied the edit to %s.", zPath);
           blob_reset(&next);
         }else{
           blob_appendf(&out, "Error: The target text to replace was not found in %s.", zPath);
         }
         blob_reset(&content);
       }else{
         blob_appendf(&out, "Error: Could not read file %s for editing.", zPath);
       }
    }else{
      /* Propose phase */
      blob_appendf(&out, "{\"type\":\"propose_edit\",\"path\":%!j,\"replace\":%!j,\"with\":%!j}", 
                   zPath, zReplace, zWith);
    }
  }else{
    blob_appendf(&out, "Error: Unknown tool %s", zTool);
  }

  Th_SetResult(interp, blob_str(&out), blob_size(&out));
  blob_reset(&out);
  return TH_OK;
}

/*
** TH1 command: agent_save SID USER ROLE KIND PROVIDER MODEL META MSG
**
** Persists a chat record to the database. Returns the acid.
*/
static int agent_save_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Blob msg = BLOB_INITIALIZER;
  int sid, acid;
  int i;
  if( argc<9 ){
    return Th_WrongNumArgs(interp, "agent_save SID USER ROLE KIND PROVIDER MODEL META MSG");
  }
  sid = atoi(argv[1]);
  for(i=8; i<argc; i++){
    if( i>8 ) blob_append(&msg, " ", 1);
    blob_append(&msg, argv[i], argl[i]);
  }
  acid = agent_chat_save(sid, argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], blob_str(&msg));
  blob_reset(&msg);
  Th_SetResultInt(interp, acid);
  return TH_OK;
}

/*
** TH1 command: agent_save_event SID USER KIND PROVIDER MODEL META MSG
**
** Persists a system event to the database.
*/
static int agent_save_event_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Blob msg = BLOB_INITIALIZER;
  int sid;
  int i;
  if( argc<8 ){
    return Th_WrongNumArgs(interp, "agent_save_event SID USER KIND PROVIDER MODEL META MSG");
  }
  sid = atoi(argv[1]);
  for(i=7; i<argc; i++){
    if( i>7 ) blob_append(&msg, " ", 1);
    blob_append(&msg, argv[i], argl[i]);
  }
  agent_chat_save_event(sid, argv[2], argv[3], argv[4], argv[5], argv[6], blob_str(&msg));
  blob_reset(&msg);
  Th_SetResultInt(interp, db_last_insert_rowid());
  return TH_OK;
}

/*
** TH1 command: agent_config KEY
**
** Returns a setting from cfg/ai-agent.json.
*/
static int agent_config_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  char *zVal;
  if( argc!=2 ) return Th_WrongNumArgs(interp, "agent_config KEY");
  zVal = agent_config_get(argv[1]);
  if( zVal ){
    Th_SetResult(interp, zVal, -1);
    fossil_free(zVal);
  }else{
    Th_SetResult(interp, "", 0);
  }
  return TH_OK;
}

/*
** TH1 command: agent_eval SID ACID PROVIDER MODEL KIND MSG
**
** Records an evaluation row for a message.
*/
static int agent_eval_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  if( argc!=7 ) return Th_WrongNumArgs(interp, "agent_eval SID ACID PROVIDER MODEL KIND MSG");
  ai_chat_eval_record(atoi(argv[1]), atoi(argv[2]), argv[3], argv[4], argv[5], argv[6]);
  return TH_OK;
}

/*
** TH1 command: pool_list_pending TIER
**
** Returns a Tcl list of nids for items that need processing to reach TIER.
** (e.g. tier 2 gets items currently at tier 1).
*/
static int pool_list_pending_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int targetTier;
  Stmt q;
  char *zRes = 0;
  int nRes = 0;
  if( argc!=2 ) return Th_WrongNumArgs(interp, "pool_list_pending TIER");
  targetTier = atoi(argv[1]);
  if( targetTier<1 ) targetTier = 1;
  db_prepare(&q, 
    "SELECT nid FROM repository.ai_note WHERE tier=%d AND duplicate_of IS NULL AND process_level IS NOT NULL",
    targetTier - 1
  );
  while( db_step(&q)==SQLITE_ROW ){
    char zBuf[32];
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%d", db_column_int(&q, 0));
    Th_ListAppend(interp, &zRes, &nRes, zBuf, -1);
  }
  db_finalize(&q);
  Th_SetResult(interp, zRes ? zRes : "", nRes);
  if( zRes ) fossil_free(zRes);
  return TH_OK;
}

/*
** TH1 command: pool_get NID
**
** Returns the text body of an ai_note.
*/
static int pool_get_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int nid;
  char *zBody;
  if( argc!=2 ) return Th_WrongNumArgs(interp, "pool_get NID");
  nid = atoi(argv[1]);
  zBody = db_text(0, "SELECT body FROM repository.ai_note WHERE nid=%d", nid);
  if( zBody ){
    Th_SetResult(interp, zBody, -1);
    fossil_free(zBody);
  }else{
    Th_SetResult(interp, "", 0);
  }
  return TH_OK;
}

/*
** TH1 command: pool_put TIER BODY [METADATA]
**
** Creates a new AI note at TIER, returning the new NID.
*/
static int pool_put_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int tier;
  const char *zBody;
  const char *zMeta = 0;
  Blob body = BLOB_INITIALIZER;
  int nid;
  if( argc!=3 && argc!=4 ) return Th_WrongNumArgs(interp, "pool_put TIER BODY ?METADATA?");
  tier = atoi(argv[1]);
  zBody = argv[2];
  if( argc==4 ) zMeta = argv[3];
  blob_append(&body, zBody, argl[2]);
  nid = ai_note_create(tier, 0, &body, "th1-pool", 0, 0, 0, zMeta, 0, 0, 0, 0, 0);
  blob_reset(&body);
  Th_SetResultInt(interp, nid);
  return TH_OK;
}

/*
** TH1 command: pool_link FROM_NID TO_NID LINK_TYPE
**
** Creates or bumps a relationship between notes.
*/
static int pool_link_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  if( argc!=4 ) return Th_WrongNumArgs(interp, "pool_link FROM_NID TO_NID LINK_TYPE");
  ai_note_link_upsert(atoi(argv[1]), atoi(argv[2]), argv[3], 1.0);
  return TH_OK;
}

/*
** TH1 command: pool_related NID LIMIT
**
** Returns a Tcl list of NIDs related to the given note via the Knowledge Graph.
*/
static int pool_related_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int nid, limit;
  char *zRes;
  if( argc!=2 && argc!=3 ) return Th_WrongNumArgs(interp, "pool_related NID ?LIMIT?");
  nid = atoi(argv[1]);
  limit = argc==3 ? atoi(argv[2]) : 5;
  zRes = ai_note_related_nids(nid, limit);
  if( zRes ){
    Th_SetResult(interp, zRes, -1);
    fossil_free(zRes);
  }else{
    Th_SetResult(interp, "", 0);
  }
  return TH_OK;
}

/*
** Register all agent-related TH1 commands.
*/
void agent_register_th1(Th_Interp *interp){
  static const struct {
    const char *zName;
    Th_CommandProc xProc;
    void *pContext;
  } aCmd[] = {
    {"agent_context",    agent_context_th1, 0},
    {"agent_last_retrieval_qid", agent_last_retrieval_qid_th1, 0},
    {"agent_run",        agent_run_th1, 0},
    {"agent_run_stream", agent_run_stream_th1, 0},
    {"agent_save",       agent_save_th1, 0},
    {"agent_save_event", agent_save_event_th1, 0},
    {"agent_config",     agent_config_th1, 0},
    {"agent_eval",       agent_eval_th1, 0},
    {"agent_mcp_call",   agent_mcp_call_th1, 0},
    {"agent_json_extract", agent_json_extract_th1, 0},
    {"agent_json_quote",   agent_json_quote_th1, 0},
    {"pool_list_pending", pool_list_pending_th1, 0},
    {"pool_get",          pool_get_th1, 0},
    {"pool_put",          pool_put_th1, 0},
    {"pool_link",         pool_link_th1, 0},
    {"pool_related",      pool_related_th1, 0},
    {0, 0, 0}
  };
  int i;
  for(i=0; aCmd[i].zName; i++){
    Th_CreateCommand(interp, aCmd[i].zName, aCmd[i].xProc, aCmd[i].pContext, 0);
  }
}

/*
** MCP Tool: list_files
*/
static void agent_mcp_list_files(void){
  Stmt q;
  int vid = db_lget_int("checkout", 0);
  int first = 1;
  CX("{\"tools\":[{\"name\":\"list_files\",\"description\":\"List files in the repo\",\"content\":[");
  db_prepare(&q, "SELECT pathname FROM vfile WHERE vid=%d AND deleted=0 ORDER BY pathname LIMIT 100", vid);
  while( db_step(&q)==SQLITE_ROW ){
    CX("%s%!j", first ? "" : ",", db_column_text(&q, 0));
    first = 0;
  }
  db_finalize(&q);
  CX("]}]}");
}

/*
** COMMAND: agent
**
** Additional SUBCOMMAND for MCP:
**
**    fossil agent mcp
**       Run Fossil as a Model Context Protocol (MCP) server on stdio.
*/
void agent_mcp_cmd(void){
  Blob line = BLOB_INITIALIZER;
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  while( blob_read_from_channel(&line, stdin, 0)>=0 ){
    const char *zLine = blob_str(&line);
    if( strstr(zLine, "\"method\":\"tools/list\"") ){
      Blob schema = BLOB_INITIALIZER;
      char *zPath = mprintf("%scfg/mcp_tools.json", g.zLocalRoot);
      if( blob_read_from_file(&schema, zPath, ExtFILE)>=0 ){
        CX("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":%s}\n", blob_str(&schema));
      }else{
        CX("{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32000,\"message\":\"MCP schema not found\"}}\n");
      }
      fossil_free(zPath);
      blob_reset(&schema);
      fflush(stdout);
    }else if( strstr(zLine, "\"method\":\"tools/call\"") && strstr(zLine, "\"name\":\"list_files\"") ){
      Stmt q;
      int vid = db_lget_int("checkout", 0);
      int first = 1;
      CX("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"");
      db_prepare(&q, "SELECT pathname FROM vfile WHERE vid=%d AND deleted=0 ORDER BY pathname LIMIT 100", vid);
      while( db_step(&q)==SQLITE_ROW ){
        CX("%s%s", first ? "" : "\\n", db_column_text(&q, 0));
        first = 0;
      }
      db_finalize(&q);
      CX("\"}]}}\n");
      fflush(stdout);
    }else if( strstr(zLine, "\"method\":\"tools/call\"") && strstr(zLine, "\"name\":\"read_file\"") ){
      const char *zPath = strstr(zLine, "\"path\":\"");
      if( zPath ){
        char *zCopy;
        zPath += 8;
        zCopy = fossil_strdup(zPath);
        for(int i=0; zCopy[i]; i++){ if(zCopy[i]=='\"'){ zCopy[i]=0; break; } }
        Blob content = BLOB_INITIALIZER;
        if( blob_read_from_file(&content, zCopy, ExtFILE)>=0 ){
          CX("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":%!j}]}}\n", blob_str(&content));
        }else{
          CX("{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32000,\"message\":\"File not found\"}}\n");
        }
        blob_reset(&content);
        fossil_free(zCopy);
      }
      fflush(stdout);
    }else if( strstr(zLine, "\"method\":\"tools/call\"") && strstr(zLine, "\"name\":\"semantic_search\"") ){
      const char *zQuery = strstr(zLine, "\"query\":\"");
      if( zQuery ){
        char *zCopy;
        zQuery += 9;
        zCopy = fossil_strdup(zQuery);
        for(int i=0; zCopy[i]; i++){ if(zCopy[i]=='\"'){ zCopy[i]=0; break; } }
        Blob out = BLOB_INITIALIZER;
        if( agent_semantic_search(agent_embedding_model(), zCopy, 5, &out, 0, 0)>0 ){
          CX("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":%!j}]}}\n", blob_str(&out));
        }else{
          CX("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"No matches found.\"}]}}\n");
        }
        blob_reset(&out);
        fossil_free(zCopy);
      }
      fflush(stdout);
    }
    blob_reset(&line);
  }
}
