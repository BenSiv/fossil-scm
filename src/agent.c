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
  const char *zMetadata
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

static int agent_generate_embedding(
  const char *zModel,
  const char *zText,
  Blob *pOut
);
static const char *agent_chat_session_model(int sid, const char *zDefault);
static const char *agent_chat_session_provider(int sid, const char *zDefault);
static const char *agent_command_template(void);
static const char *agent_embedding_template(void);

/*
** Repo-local config file for agent integration. When present, this file
** overrides the corresponding Fossil settings for the agent runtime.
*/
static const char zAgentConfigFile[] = "cfg/ai-agent.json";
static const char *zAgentConfigPath = 0;

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
  if( zCmd==0 || zCmd[0]==0 ) return "unset";
  if( strstr(zCmd, "fossil-codex-agent.sh")!=0
   || strstr(zCmd, " codex")!=0
   || fossil_strncmp(zCmd, "codex", 5)==0
  ){
    return "codex";
  }
  if( strstr(zCmd, "fossil-ollama-agent.sh")!=0
   || strstr(zCmd, " ollama")!=0
   || fossil_strncmp(zCmd, "ollama", 6)==0
  ){
    return "ollama";
  }
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

/*
** Look up a string value in cfg/ai-agent.json. Returns a newly allocated
** string on success or NULL if the config file/key is missing or invalid.
** The caller must fossil_free() the result.
*/
static char *agent_config_get(const char *zKey){
#ifdef FOSSIL_ENABLE_JSON
  char *zPath = 0;
  Blob json = BLOB_INITIALIZER;
  cson_parse_info pinfo = cson_parse_info_empty;
  cson_value *pRoot = 0;
  cson_object *pObj = 0;
  cson_value *pVal = 0;
  const char *zVal = 0;
  char *zOut = 0;

  zPath = agent_config_path();
  if( zPath==0 ) return 0;
  if( file_size(zPath, ExtFILE)<0 ){
    fossil_free(zPath);
    return 0;
  }
  if( blob_read_from_file(&json, zPath, ExtFILE)<0 ){
    blob_reset(&json);
    fossil_free(zPath);
    return 0;
  }
  pRoot = cson_parse_Blob(&json, &pinfo);
  if( pRoot==0 || !cson_value_is_object(pRoot) ){
    cson_value_free(pRoot);
    blob_reset(&json);
    fossil_free(zPath);
    return 0;
  }
  pObj = cson_value_get_object(pRoot);
  pVal = cson_object_get(pObj, zKey);
  zVal = pVal ? cson_value_get_cstr(pVal) : 0;
  if( zVal && zVal[0] ){
    zOut = mprintf("%s", zVal);
  }
  cson_value_free(pRoot);
  blob_reset(&json);
  fossil_free(zPath);
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
  if( zCmd==0 ) zCmd = db_get("agent-command", "ollama run %m");
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
    zCached = mprintf("ollama");
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
  return zCached ? zCached : db_get("agent-command", "ollama run %m");
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
  return zProvider && fossil_strcmp(zProvider, "ollama")==0;
}

/*
** Return non-zero if the provider name is one of Fossil's built-in
** compatibility backends.
*/
static int agent_provider_is_known(const char *zProvider){
  return zProvider
      && (fossil_strcmp(zProvider, "codex")==0
       || fossil_strcmp(zProvider, "ollama")==0
       || fossil_strcmp(zProvider, "custom")==0);
}

/*
** Emit a JSON object describing the effective chat and embedding config for
** sidCurrent. If sidCurrent refers to an existing session, chat provider/model
** reflect that session rather than the current default.
*/
static void agent_emit_config_json(int sidCurrent){
  const char *zChatProvider = agent_chat_session_provider(
    sidCurrent, agent_chat_provider()
  );
  const char *zChatModel = agent_chat_session_model(
    sidCurrent, agent_default_model()
  );
  const char *zEmbedProvider = agent_embedding_provider();
  const char *zEmbedModel = agent_embedding_model();
  const char *zCmd = agent_command_template();
  const char *zEmbedCmd = agent_embedding_template();
  char *zSource = agent_config_source();
  int chatProviderLocked = 1;
  int chatSupportsStreaming = 0;
  int chatSupportsModelDiscovery = 0;
  int embeddingAvailable = agent_embedding_is_available();
  int embeddingSupportsModelDiscovery = 0;
  int chatProviderKnown = agent_provider_is_known(zChatProvider);
  int embeddingProviderKnown = agent_provider_is_known(zEmbedProvider);
  CX("{\"sid\":%d,\"source\":%!j,\"chat_provider\":%!j,\"chat_command\":%!j,"
     "\"chat_model\":%!j,\"embedding_provider\":%!j,"
     "\"embedding_command\":%!j,\"embedding_model\":%!j,"
     "\"chat_provider_locked\":%d,\"chat_supports_streaming\":%d,"
     "\"chat_supports_model_discovery\":%d,\"embedding_available\":%d,"
     "\"embedding_supports_model_discovery\":%d,"
     "\"chat_provider_known\":%d,\"embedding_provider_known\":%d}\n",
     sidCurrent, zSource, zChatProvider, zCmd, zChatModel,
     zEmbedProvider, zEmbedCmd, zEmbedModel,
     chatProviderLocked, chatSupportsStreaming, chatSupportsModelDiscovery,
     embeddingAvailable, embeddingSupportsModelDiscovery,
     chatProviderKnown, embeddingProviderKnown);
  fossil_free(zSource);
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
  if( fossil_stricmp(zProvider, "codex")==0 ){
    if( fossil_stricmp(zModel, "auto")==0 ) return 0;
    if( agent_model_looks_ollama(zModel) ){
      blob_appendf(pErr,
        "model \"%s\" looks like an Ollama model but provider is codex", zModel
      );
      return 1;
    }
  }else if( fossil_stricmp(zProvider, "ollama")==0
         || fossil_stricmp(zProvider, "ollama-http")==0 ){
    if( fossil_stricmp(zModel, "auto")==0 ){
      blob_appendf(pErr,
        "model \"auto\" is not valid for provider %s", zProvider
      );
      return 1;
    }
  }
  return 0;
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
@   provider TEXT,
@   model TEXT,
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

/*
** Resolve the current session id for zUser, creating one if needed.
*/
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

/*
** Return the most recent session id for zUser without creating a new one.
*/
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
static void agent_chat_save(
  int sid,
  const char *zUser,
  const char *zRole,
  const char *zProvider,
  const char *zModel,
  const char *zMsg
){
  if( zMsg==0 || zMsg[0]==0 ) return;
  agent_chat_create_tables();
  db_multi_exec(
    "INSERT INTO agentchat(sid,mtime,xfrom,role,provider,model,msg)"
    " VALUES(%d,julianday('now'),%Q,%Q,%Q,%Q,%Q)",
    sid,
    zUser ? zUser : "",
    zRole ? zRole : "agent",
    zProvider ? zProvider : "",
    zModel ? zModel : "",
    zMsg
  );
  agent_chat_session_touch(sid, zMsg, zProvider, zModel);
}

/*
** Emit session list for the current user.
*/
static void agent_chat_render_sessions(const char *zUser, int sidCurrent){
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
    @ <div>
    if( sid==sidCurrent ){
      @ <b>%h(zTitle)</b> <span class="dimmed">[%h(zProvider)%s(zModel&&zModel[0]?" / ":"")%h(zModel)]</span>
    }else{
      @ <a href="%R/agentui?sid=%d(sid)">%h(zTitle)</a> <span class="dimmed">[%h(zProvider)%s(zModel&&zModel[0]?" / ":"")%h(zModel)]</span>
    }
    @ </div>
  }
  db_finalize(&q);
}

/*
** Emit recent saved agent chat messages for a session into the page log.
*/
static void agent_chat_render_history(int sidCurrent){
  Stmt q;
  int nLimit = db_get_int("agent-history-count", 50);
  if( nLimit<=0 || sidCurrent<=0 ) return;
  if( !db_table_exists("repository","agentchat") ) return;
  db_prepare(&q,
    "SELECT role, provider, model, msg FROM agentchat"
    " WHERE sid=%d"
    " ORDER BY acid ASC LIMIT %d",
    sidCurrent, nLimit
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zRole = db_column_text(&q, 0);
    const char *zProvider = db_column_text(&q, 1);
    const char *zModel = db_column_text(&q, 2);
    const char *zMsg = db_column_text(&q, 3);
    @ <div style="margin-bottom:0.8em;">
    @ <b>%h(zRole && fossil_strcmp(zRole,"user")==0 ? "You" : "Agent"):</b>
    if( zProvider && zProvider[0] ){
      @ <span class="dimmed">[%h(zProvider)%s(zModel&&zModel[0]?" / ":"")%h(zModel)]</span>
    }
    @ <pre style="white-space:pre-wrap;display:inline;margin:0">%h(zMsg)</pre>
    @ </div>
  }
  db_finalize(&q);
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
  int bVerbose
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
static int agent_assemble_context(
  Blob *pOut,
  const char *zModel,
  const char *zQuery
){
  int vid;
  Stmt q;
  int nAdded = 0;
  vid = db_table_exists("localdb", "vvar") ? db_lget_int("checkout", 0) : 0;
  if( vid ){
    int nFile = 0;
    blob_appendf(pOut, "--- REPOSITORY CONTEXT ---\n");
    blob_appendf(pOut, "File Structure (top 100 files):\n");
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
    blob_appendf(pOut, "\nPending Changes:\n");
    if( agent_changes_text(pOut, vid, "  ")==0 ){
      blob_appendf(pOut, "  (none)\n");
    }
    nAdded = 1;
  }

  if( zQuery && zQuery[0] ){
    int nBefore = blob_size(pOut);
    if( !nAdded ){
      blob_appendf(pOut, "--- REPOSITORY CONTEXT ---\n");
    }
    agent_semantic_search(zModel, zQuery, 3, pOut, 0);
    if( blob_size(pOut)>nBefore ) nAdded = 1;
  }
  if( nAdded ){
    blob_appendf(pOut, "--- END CONTEXT ---\n\n");
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
static void agent_strip_prefix_noise(Blob *pText);

static int agent_run_backend(
  const char *zProvider,
  const char *zModel,
  const char *zPrompt,
  Blob *pReply,
  Blob *pErr
){
  Blob cmd = BLOB_INITIALIZER;
  Blob envCmd = BLOB_INITIALIZER;
  FILE *in;
  FILE *out = 0;
  int fdIn = -1;
  int childPid = 0;
  int rc;
  const char *zCmdTmpl = agent_command_template();

  blob_zero(pReply);
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
  /* Send the prompt via stdin and close it so the child doesn't wait. */
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
  blob_read_from_channel(pReply, in, -1);
  pclose2(fdIn, out, childPid);
  agent_strip_ansi(pReply);
  agent_strip_prefix_noise(pReply);
  blob_trim(pReply);
  if( blob_size(pReply)==0 ){
    blob_appendf(pErr,
      "agent command failed for model \"%s\"", zModel
    );
    blob_reset(&cmd);
    blob_reset(&envCmd);
    return 1;
  }
  if( fossil_strncmp(blob_str(pReply), "Error:", 6)==0 ){
    blob_append(pErr, blob_str(pReply), blob_size(pReply));
    blob_reset(&cmd);
    blob_reset(&envCmd);
    return 1;
  }
  blob_reset(&cmd);
  blob_reset(&envCmd);
  return 0;
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
  Blob body = BLOB_INITIALIZER;
  int tier = 1;

  zTitle = find_option("title", 0, 1);
  zTier = find_option("tier", 0, 1);
  zSourceType = find_option("source-type", 0, 1);
  zSourceRef = find_option("source-ref", 0, 1);
  zProcessLevel = find_option("process-level", 0, 1);
  zMetadata = find_option("metadata", 0, 1);
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
          " [--process-level LEVEL] [--metadata JSON]");
  }
  ai_require_enabled();
  ai_note_create(
    tier, zTitle, &body, zSourceType, 0, zSourceRef, zProcessLevel, zMetadata
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
  if( agent_semantic_search(zModel, g.argv[3], nLimit, &out, 1)==0 ){
    fossil_print("No notes matched.\n");
  }else{
    fossil_print("%s", blob_str(&out));
  }
  blob_reset(&out);
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
**    fossil agent semantic-index
**       Generate embeddings for all notes and store them in ai_vector.
**
**    fossil agent note ?FILE? [--title TEXT] [--tier N]
**                             [--source-type TYPE] [--source-ref REF]
**                             [--process-level LEVEL] [--metadata JSON]
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
  }else if( fossil_strcmp(zCmd, "semantic-index")==0 ){
    agent_semantic_index_cmd();
  }else if( fossil_strcmp(zCmd, "wiki-sync")==0 ){
    agent_wiki_sync_cmd();
  }else{
    fossil_fatal("unknown agent subcommand: %s", zCmd);
  }
}

/*
** WEBPAGE: agentui
**
** Minimal manager-facing chat UI for local agent testing.
*/
void agentui_page(void){
  const char *zModel;
  const char *zSessionProvider;
  const char *zCmd;
  const char *zEmbedModel;
  const char *zEmbedCmd;
  const char *zProvider;
  const char *zEmbedProvider;
  const char *zUser;
  char *zConfigSource;
  int sidCurrent;
  int sidRequested;

  login_check_credentials();
  if( !g.perm.Read ){
    login_needed(g.anon.Read);
    return;
  }
  zUser = (g.zLogin && g.zLogin[0]) ? g.zLogin : "guest";
  sidRequested = atoi(PD("sid","0"));
  sidCurrent = agent_chat_session_exists(sidRequested) ? sidRequested : 0;
  zSessionProvider = agent_chat_session_provider(sidCurrent, agent_chat_provider());
  zModel = agent_chat_session_model(sidCurrent, agent_default_model());
  zCmd = agent_command_template();
  zEmbedModel = agent_embedding_model();
  zEmbedCmd = agent_embedding_template();
  zProvider = zSessionProvider;
  zEmbedProvider = agent_embedding_provider();
  zConfigSource = agent_config_source();
  style_set_current_feature("agent");
  style_header("Agent Chat");
  @ <div class="fossil-doc" data-title="Agent Chat">
  @ <p>This page sends prompts to the AI backend configured by the repository
  @ settings.</p>
  @ <div style="border:1px solid #888;padding:0.6em;margin:0 0 1em 0;background:rgba(127,127,127,0.05);">
  @ <b>Effective config</b><br>
  @ source: <span id="agent-config-source">%h(zConfigSource)</span><br>
  @ chat provider: <span id="agent-config-chat-provider">%h(zProvider)</span><br>
  @ chat command: <span id="agent-config-chat-command">%h(zCmd)</span><br>
  @ chat model: <span id="agent-config-chat-model">%h(zModel && zModel[0] ? zModel : "(unset)")</span><br>
  @ embedding provider: <span id="agent-config-embedding-provider">%h(zEmbedProvider)</span><br>
  @ embedding command: <span id="agent-config-embedding-command">%h(zEmbedCmd && zEmbedCmd[0] ? zEmbedCmd : "(builtin/default)")</span><br>
  @ embedding model: <span id="agent-config-embedding-model">%h(zEmbedModel && zEmbedModel[0] ? zEmbedModel : "(unset)")</span><br>
  @ capabilities:
  @ <span id="agent-config-capabilities">
  @ chat streaming=no, model discovery=no, provider locked=yes, embeddings=%s(agent_embedding_is_available() ? "yes" : "no")
  @ </span>
  @ </div>
  @ <div style="display:grid;grid-template-columns:minmax(12em,16em) 1fr;gap:1em;">
  @ <div>
  @ <div style="margin-bottom:0.7em;"><a class="btn" href="%R/agentui?new=1">New Chat</a></div>
  @ <div style="border:1px solid #888;padding:0.6em;background:rgba(127,127,127,0.05);">
  @ <div style="font-weight:bold;margin-bottom:0.4em;">Sessions</div>
  agent_chat_render_sessions(zUser, sidCurrent);
  @ </div>
  @ </div>
  @ <div>
  @ <div class="forumEdit">
  @ <label for="agent-provider"><b>Provider:</b></label>
  @ <select id="agent-provider" disabled>
  @ <option value="%h(zProvider)" selected>%h(zProvider)</option>
  @ </select>
  @ &nbsp;&nbsp;
  @ <label for="agent-model"><b>Model:</b></label>
  @ <input type="text" id="agent-model" size="24" value="%h(zModel)">
  @ &nbsp;&nbsp;
  @ <label><input type="checkbox" id="agent-context" checked> <b>Context Awareness</b></label>
  @ </div>
  @ <div id="agent-chat-log"
  @  style="min-height:320px;max-height:520px;overflow:auto;border:1px solid
  @  #888;padding:0.8em;margin:1em 0;background:rgba(127,127,127,0.05);">
  agent_chat_render_history(sidCurrent);
  @ </div>
  @ <div class="forumEdit">
  @ <textarea id="agent-chat-input" rows="6" cols="80"
  @  placeholder="Ask the local agent a question..."></textarea>
  @ </div>
  @ <div class="forumEdit">
  @ <input type="button" class="btn" id="agent-chat-send" value="Send">
  @ </div>
  @ <script nonce="%h(style_nonce())">
  @ (function(){
  @   var sid = %d(sidCurrent);
  @   var input = document.getElementById('agent-chat-input');
  @   var send = document.getElementById('agent-chat-send');
  @   var provider = document.getElementById('agent-provider');
  @   var model = document.getElementById('agent-model');
  @   var context = document.getElementById('agent-context');
  @   var log = document.getElementById('agent-chat-log');
  @   var configSource = document.getElementById('agent-config-source');
  @   var cfgChatProvider = document.getElementById('agent-config-chat-provider');
  @   var cfgChatCommand = document.getElementById('agent-config-chat-command');
  @   var cfgChatModel = document.getElementById('agent-config-chat-model');
  @   var cfgEmbedProvider = document.getElementById('agent-config-embedding-provider');
  @   var cfgEmbedCommand = document.getElementById('agent-config-embedding-command');
  @   var cfgEmbedModel = document.getElementById('agent-config-embedding-model');
  @   var cfgCapabilities = document.getElementById('agent-config-capabilities');
  @   function yesNo(v){
  @     return v ? 'yes' : 'no';
  @   }
  @   function showValue(node, value, fallback){
  @     if(node) node.textContent = value && value.length ? value : fallback;
  @   }
  @   function applyConfig(data){
  @     if(data.chat_provider){
  @       provider.innerHTML = '';
  @       var opt = document.createElement('option');
  @       opt.value = data.chat_provider;
  @       opt.textContent = data.chat_provider;
  @       opt.selected = true;
  @       provider.appendChild(opt);
  @     }
  @     if(data.chat_model!==undefined){ model.value = data.chat_model || ''; }
  @     showValue(configSource, data.source, '(unknown)');
  @     showValue(cfgChatProvider, data.chat_provider, '(unset)');
  @     showValue(cfgChatCommand, data.chat_command, '(unset)');
  @     showValue(cfgChatModel, data.chat_model, '(unset)');
  @     showValue(cfgEmbedProvider, data.embedding_provider, '(unset)');
  @     showValue(cfgEmbedCommand, data.embedding_command, '(builtin/default)');
  @     showValue(cfgEmbedModel, data.embedding_model, '(unset)');
  @     if(cfgCapabilities){
  @       cfgCapabilities.textContent =
  @         'chat streaming=' + yesNo(data.chat_supports_streaming)
  @         + ', model discovery=' + yesNo(data.chat_supports_model_discovery)
  @         + ', provider locked=' + yesNo(data.chat_provider_locked)
  @         + ', embeddings=' + yesNo(data.embedding_available);
  @     }
  @   }
  @   function addMsg(role, text){
  @     var div = document.createElement('div');
  @     div.style.marginBottom = '0.8em';
  @     div.innerHTML = '<b>'+role+':</b> <pre style="white-space:pre-wrap;display:inline;margin:0">'
  @       + text.replace(/[&<>]/g, function(c){
  @           return {'&':'&amp;','<':'&lt;','>':'&gt;'}[c];
  @         })
  @       + '</pre>';
  @     log.appendChild(div);
  @     log.scrollTop = log.scrollHeight;
  @   }
  @   log.scrollTop = log.scrollHeight;
  @   fetch('agent-config?sid='+encodeURIComponent(sid)).then(function(r){
  @     return r.json();
  @   }).then(function(data){
  @     applyConfig(data);
  @   }).catch(function(){});
  @   send.addEventListener('click', function(){
  @     var msg = input.value.trim();
  @     if(!msg) return;
  @     addMsg('You', msg);
  @     input.value = '';
  @     fetch('agent-chat', {
  @       method: 'POST',
  @       headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
  @       body: new URLSearchParams({sid: sid, msg: msg, provider: provider.value, model: model.value, context: context.checked ? 1 : 0})
  @     }).then(function(r){
  @       return r.text().then(function(text){
  @         var data;
  @         try{
  @           data = JSON.parse(text);
  @         }catch(e){
  @           throw new Error(text ? text.slice(0, 240) : ('HTTP ' + r.status));
  @         }
  @         if(!r.ok){
  @           throw new Error(data.error || data.reply || ('HTTP ' + r.status));
  @         }
  @         return data;
  @       });
  @     }).then(function(data){
  @       sid = data.sid || sid;
  @       if(data.provider || data.model){
  @         applyConfig({chat_provider: data.provider, chat_model: data.model});
  @       }
  @       addMsg('Agent', data.reply || data.error || '(no reply)');
  @     }).catch(function(err){
  @       addMsg('Agent', 'Request failed: ' + err);
  @     });
  @   });
  @   input.addEventListener('keydown', function(e){
  @     if((e.ctrlKey || e.metaKey) && e.key==='Enter') send.click();
  @   });
  @ })();
  @ </script>
  @ </div>
  @ </div>
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
** WEBPAGE: agent-chat
**
** JSON endpoint for the configured agent chat UI.
*/
void agent_chat_page(void){
  Blob reply = BLOB_INITIALIZER;
  Blob err = BLOB_INITIALIZER;
  const char *zMsg;
  const char *zModel;
  const char *zProvider;
  const char *zUser;
  int sid;
  int rc;
  char *zContextMsg = 0;

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
  if( zMsg[0]==0 ){
    CX("{\"error\":%!j}\n", "missing msg parameter");
    return;
  }
  if( zModel[0]==0 ){
    CX("{\"error\":%!j}\n", "missing model parameter");
    return;
  }
  if( agent_validate_provider_model(zProvider, zModel, &err) ){
    CX("{\"error\":%!j}\n", blob_str(&err));
    blob_reset(&err);
    return;
  }
  db_begin_write();
  db_unprotect(PROTECT_READONLY);
  if( PB("context") ){
    Blob ctx = BLOB_INITIALIZER;
    if( agent_assemble_context(&ctx, zModel, zMsg) ){
      blob_appendf(&ctx, "User request:\n%s\n", zMsg);
      zContextMsg = fossil_strdup(blob_str(&ctx));
      zMsg = zContextMsg;
    }
    blob_reset(&ctx);
  }
  if( sid<=0 || !agent_chat_session_exists(sid) ){
    sid = agent_chat_session_create(zUser, zProvider, zModel);
  }
  agent_chat_save(sid, zUser, "user", zProvider, zModel, PD("msg",""));
  rc = agent_run_backend(zProvider, zModel, zMsg, &reply, &err);
  if( rc==0 ){
    agent_chat_save(sid, zUser, "agent", zProvider, zModel, blob_str(&reply));
    db_end_transaction(0);
    CX("{\"sid\":%d,\"provider\":%!j,\"model\":%!j,\"reply\":%!j}\n",
      sid, zProvider, zModel, blob_str(&reply));
  }else{
    const char *zErr = blob_size(&err)>0 ? blob_str(&err)
                                         : "agent invocation failed";
    agent_chat_save(sid, zUser, "agent", zProvider, zModel, zErr);
    db_end_transaction(0);
    CX("{\"sid\":%d,\"provider\":%!j,\"model\":%!j,\"error\":%!j}\n",
      sid, zProvider, zModel, zErr);
  }
  if( PB("context") ) fossil_free((char*)zMsg);
  blob_reset(&reply);
  blob_reset(&err);
}
