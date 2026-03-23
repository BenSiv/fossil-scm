/*
** Copyright (c) 2026
**
** Web-facing handlers for the Fossil agent surfaces.
*/
#include "config.h"
#include "agent.h"
#include "agent_internal.h"

#define PROTECT_READONLY   0x08
void cgi_set_content_type(const char *zType);
void db_unprotect(unsigned flags);
char *style_nonce(void);
int Th_Render(const char *z);
const unsigned char *builtin_file(const char *zFilename, int *piSize);

void ai_chat_eval_feedback(int sid, int acid, const char *zFeedback);
void agent_emit_pool_json(void);
void agent_emit_retrieval_json(int qid);

int agent_load_asset(const char *zAsset, Blob *pOut){
  char *zPath = 0;
  const unsigned char *pData = 0;
  int nData = 0;
  blob_zero(pOut);
  if( g.zLocalRoot && g.zLocalRoot[0] ){
    zPath = mprintf("%s%s", g.zLocalRoot, zAsset);
    if( file_size(zPath, ExtFILE)>=0
     && blob_read_from_file(pOut, zPath, ExtFILE)>=0
    ){
      fossil_free(zPath);
      return 1;
    }
    fossil_free(zPath);
  }
  pData = builtin_file(zAsset, &nData);
  if( pData==0 ){
    char *zBuiltin = mprintf("../%s", zAsset);
    pData = builtin_file(zBuiltin, &nData);
    fossil_free(zBuiltin);
  }
  if( pData && nData>=0 ){
    blob_append(pOut, (const char*)pData, nData);
    return 1;
  }
  return 0;
}

const char *agent_orchestration_script(const char *zRole, Blob *pScript){
  char *zAsset = 0;
  const char *zUseRole = (zRole && zRole[0]) ? zRole : "default";
  zAsset = mprintf("cfg/roles/%s.th1", zUseRole);
  if( agent_load_asset(zAsset, pScript) ){
    fossil_free(zAsset);
    return blob_str(pScript);
  }
  fossil_free(zAsset);
  return 0;
}

static void agent_api_v1_emit_capabilities(void){
  CX("{"
     "\"backend\":\"fossil\","
     "\"chat\":true,"
     "\"events\":\"poll-only\","
     "\"requestCancel\":false,"
     "\"sessionCreate\":true,"
     "\"sessionRename\":true,"
     "\"sessionDelete\":false,"
     "\"sessionFork\":false,"
     "\"toolRegistry\":true"
     "}");
}

static void agent_api_v1_emit_error(
  const char *zError,
  const char *zCode
){
  CX("{\"api_version\":\"v1\",\"ok\":false,\"error\":%!j", zError);
  if( zCode && zCode[0] ){
    CX(",\"error_code\":%!j", zCode);
  }
  CX(",\"capabilities\":");
  agent_api_v1_emit_capabilities();
  CX("}\n");
}

/*
** WEBPAGE: agentui
**
** Main interactive agent console.
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

  {
    Blob history = BLOB_INITIALIZER;
    Blob sessions = BLOB_INITIALIZER;
    Blob template = BLOB_INITIALIZER;
    Blob css = BLOB_INITIALIZER;
    Blob js = BLOB_INITIALIZER;

    agent_chat_render_history_to_blob(sidCurrent, &history);
    agent_chat_render_sessions_to_blob(zUser, sidCurrent, &sessions);
    Th_SetVar(g.interp, "history_html", 12, blob_str(&history), blob_size(&history));
    Th_SetVar(g.interp, "sessions_html", 13, blob_str(&sessions), blob_size(&sessions));

    if( agent_load_asset("cfg/agentui.css", &css) ){
      Th_SetVar(g.interp, "ui_css", 6, blob_str(&css), blob_size(&css));
    }

    if( agent_load_asset("cfg/agentui.js", &js) ){
      Th_SetVar(g.interp, "ui_js", 5, blob_str(&js), blob_size(&js));
    }

    if( agent_load_asset("cfg/agentui.th1", &template) ){
      Th_Render(blob_str(&template));
    }else{
      CX("<p class=\"error\">Error: agent UI template asset not found</p>");
    }
    blob_reset(&history);
    blob_reset(&sessions);
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
** JSON config for the current agent session.
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
** JSON history for a chat session.
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
** JSON event log for a chat session.
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
** WEBPAGE: agent-feedback
**
** Persist usefulness feedback for a terminal agent reply.
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
** WEBPAGE: agent-pool
**
** JSON view of the knowledge pool.
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
** JSON view of a retrieval query and its matches.
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
** JSON endpoint implementation for non-streaming agent chat.
*/
static void agent_chat_page_impl(int bApiV1){
  Blob err = BLOB_INITIALIZER;
  Blob script = BLOB_INITIALIZER;
  const char *zMsg;
  const char *zModel;
  const char *zProvider;
  const char *zUser;
  const char *zRequestIdParam;
  const char *zUseRequestId;
  int sid;
  int rid = 0;
  int terminalAcid = 0;
  char *zRequestId = 0;

  login_check_credentials();
  if( !g.perm.Read ){
    cgi_set_content_type("application/json");
    if( bApiV1 ){
      agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    }else{
      CX("{\"error\":%!j}\n", "missing read permissions or not logged in");
    }
    return;
  }
  zMsg = PD("msg", "");
  zProvider = PD("provider", agent_chat_provider());
  zModel = PD("model", agent_default_model());
  zUser = (g.zLogin && g.zLogin[0]) ? g.zLogin : "guest";
  zRequestIdParam = PD("request_id", "");
  sid = atoi(PD("sid","0"));
  cgi_set_content_type("application/json");

  if( zMsg[0]==0 || zModel[0]==0 ){
    if( bApiV1 ){
      agent_api_v1_emit_error("missing msg or model parameter", "missing_parameter");
    }else{
      CX("{\"error\":%!j}\n", "missing msg or model parameter");
    }
    return;
  }
  if( agent_validate_provider_model(zProvider, zModel, &err) ){
    if( bApiV1 ){
      agent_api_v1_emit_error(blob_str(&err), "invalid_provider_model");
    }else{
      CX("{\"error\":%!j}\n", blob_str(&err));
    }
    blob_reset(&err);
    return;
  }

  db_begin_write();
  db_unprotect(PROTECT_READONLY);
  if( sid<=0 || !agent_chat_session_exists(sid) ){
    sid = agent_chat_session_create(zUser, zProvider, zModel);
  }
  rid = agent_request_create(sid, zRequestIdParam, "running");
  zUseRequestId = agent_chat_session_request_id(sid);
  zRequestId = mprintf("%s", zUseRequestId ? zUseRequestId : "");

  Th_FossilInit(TH_INIT_DEFAULT);
  Th_StoreInt("sid", sid);
  Th_SetVar(g.interp, "msg", 3, zMsg, (int)strlen(zMsg));
  Th_SetVar(g.interp, "provider", 8, zProvider, (int)strlen(zProvider));
  Th_SetVar(g.interp, "model", 5, zModel, (int)strlen(zModel));
  Th_SetVar(g.interp, "user", 4, zUser, (int)strlen(zUser));
  Th_SetVar(g.interp, "request_id", 10, zRequestId, -1);
  Th_StoreInt("context_enabled", PB("context"));

  if( agent_orchestration_script("json-default", &script)==0 ){
    agent_request_set_state(rid, "failed", 0);
    if( bApiV1 ){
      agent_api_v1_emit_error("missing default orchestration script", "missing_asset");
    }else{
      CX("{\"error\":%!j}\n", "missing default orchestration script");
    }
  }else{
    int thRc = Th_Eval(g.interp, 0, blob_str(&script), -1);
    int nResult = 0;
    const char *zResult = Th_GetResult(g.interp, &nResult);
    if( thRc==TH_ERROR ){
      terminalAcid = agent_chat_latest_terminal_acid(sid);
      agent_request_set_state(rid, "failed", terminalAcid);
      if( bApiV1 ){
        agent_api_v1_emit_error(zResult ? zResult : "TH1 eval failed", "th1_error");
      }else{
        CX("{\"error\":%!j}\n", zResult ? zResult : "TH1 eval failed");
      }
    }else{
      terminalAcid = agent_chat_latest_terminal_acid(sid);
      agent_request_set_state(rid, "finished", terminalAcid);
      if( bApiV1 ){
        CX("{\"api_version\":\"v1\",\"ok\":true,\"capabilities\":");
        agent_api_v1_emit_capabilities();
        CX(",\"chat\":");
        CX("%.*s", nResult, zResult ? zResult : "{}");
        CX(",\"request_id\":%!j", zRequestId && zRequestId[0] ? zRequestId : zRequestIdParam);
        CX(",\"request\":");
        agent_emit_request_object_json(sid, zRequestId && zRequestId[0] ? zRequestId : zRequestIdParam);
        CX("}\n");
      }else{
        CX("%.*s\n", nResult, zResult ? zResult : "{}");
      }
    }
  }
  db_end_transaction(0);
  blob_reset(&script);
  blob_reset(&err);
  fossil_free(zRequestId);
}

/*
** WEBPAGE: agent-chat
**
** JSON endpoint for non-streaming agent chat.
*/
void agent_chat_page(void){
  agent_chat_page_impl(0);
}

/*
** WEBPAGE: agent-chat-stream
**
** SSE endpoint for streaming agent chat.
*/
void agent_chat_stream_page(void){
  Blob script = BLOB_INITIALIZER;
  const char *zMsg;
  const char *zModel;
  const char *zProvider;
  const char *zUser;
  const char *zRoleParam;
  const char *zRequestIdParam;
  const char *zUseRequestId;
  char *zRequestId = 0;
  int rid = 0;
  int terminalAcid = 0;
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
  zRoleParam = PD("role", "");
  zRequestIdParam = PD("request_id", "");

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
  rid = agent_request_create(sid, zRequestIdParam, "running");
  zUseRequestId = agent_chat_session_request_id(sid);
  zRequestId = mprintf("%s", zUseRequestId ? zUseRequestId : "");

  Th_FossilInit(TH_INIT_DEFAULT);
  Th_StoreInt("sid", sid);
  Th_SetVar(g.interp, "msg", 3, zMsg, (int)strlen(zMsg));
  Th_SetVar(g.interp, "provider", 8, zProvider, (int)strlen(zProvider));
  Th_SetVar(g.interp, "model", 5, zModel, (int)strlen(zModel));
  Th_SetVar(g.interp, "user", 4, zUser, (int)strlen(zUser));
  Th_SetVar(g.interp, "request_id", 10, zRequestId, -1);
  Th_StoreInt("context_enabled", PB("context"));

  if( agent_orchestration_script(zRoleParam, &script)==0 ){
    agent_request_set_state(rid, "failed", 0);
    CX("data: {\"error\":\"Role script not found: %s\"}\n\n",
       zRoleParam[0] ? zRoleParam : "default");
  }else{
    int thRc = Th_Eval(g.interp, 0, blob_str(&script), -1);
    if( thRc==TH_ERROR ){
      int nResult = 0;
      const char *zResult = Th_GetResult(g.interp, &nResult);
      terminalAcid = agent_chat_latest_terminal_acid(sid);
      agent_request_set_state(rid, "failed", terminalAcid);
      CX("data: {\"error\":%!j}\n\n", zResult ? zResult : "TH1 eval failed");
    }else{
      terminalAcid = agent_chat_latest_terminal_acid(sid);
      if( fossil_strcmp(agent_chat_session_request_state(sid), "waiting-approval")!=0 ){
        agent_request_set_state(rid, "finished", terminalAcid);
      }
    }
  }
  db_end_transaction(0);
  blob_reset(&script);
  fossil_free(zRequestId);
}

/*
** WEBPAGE: agent-api/v1/sessions
**
** Versioned session list API.
*/
void agent_api_v1_sessions_page(void){
  const char *zUser;
  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    return;
  }
  zUser = (g.zLogin && g.zLogin[0]) ? g.zLogin : "guest";
  CX("{\"api_version\":\"v1\",\"ok\":true,\"user\":%!j,\"capabilities\":", zUser);
  agent_api_v1_emit_capabilities();
  CX(",\"sessions\":");
  agent_emit_session_array_json(zUser);
  CX("}\n");
}

/*
** WEBPAGE: agent-api-v1-sessions
**
** Fossil-native flat alias for the versioned session list API.
*/
void agent_api_v1_sessions_flat_page(void){
  agent_api_v1_sessions_page();
}

/*
** WEBPAGE: agent-api/v1/session
**
** Versioned session detail API. Query parameter: sid.
*/
void agent_api_v1_session_page(void){
  int sidRequested;
  int sidCurrent;
  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    return;
  }
  sidRequested = atoi(PD("sid","0"));
  if( sidRequested<=0 || !agent_chat_session_exists(sidRequested) ){
    agent_api_v1_emit_error("missing or unknown sid parameter", "unknown_session");
    return;
  }
  sidCurrent = sidRequested;
  CX("{\"api_version\":\"v1\",\"ok\":true,\"capabilities\":");
  agent_api_v1_emit_capabilities();
  CX(",\"session\":");
  agent_emit_history_object_json(sidCurrent);
  CX("}\n");
}

/*
** WEBPAGE: agent-api-v1-session
**
** Fossil-native flat alias for the versioned session detail API.
*/
void agent_api_v1_session_flat_page(void){
  agent_api_v1_session_page();
}

/*
** WEBPAGE: agent-api/v1/session/create
**
** Versioned session creation API.
*/
void agent_api_v1_session_create_page(void){
  const char *zProvider;
  const char *zModel;
  const char *zUser;
  const char *zTitle;
  int sid;
  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    return;
  }
  zProvider = PD("provider", agent_chat_provider());
  zModel = PD("model", agent_default_model());
  zTitle = PD("title", "");
  zUser = (g.zLogin && g.zLogin[0]) ? g.zLogin : "guest";
  db_begin_write();
  db_unprotect(PROTECT_READONLY);
  sid = agent_chat_session_create(zUser, zProvider, zModel);
  if( zTitle[0] ){
    agent_chat_session_rename(sid, zTitle);
  }
  db_end_transaction(0);
  CX("{\"api_version\":\"v1\",\"ok\":true,\"capabilities\":");
  agent_api_v1_emit_capabilities();
  CX(",\"session\":");
  agent_emit_history_object_json(sid);
  CX("}\n");
}

/*
** WEBPAGE: agent-api-v1-session-create
**
** Fossil-native flat alias for the versioned session creation API.
*/
void agent_api_v1_session_create_flat_page(void){
  agent_api_v1_session_create_page();
}

/*
** WEBPAGE: agent-api/v1/session/name
**
** Versioned session rename API. Parameters: sid, name.
*/
void agent_api_v1_session_name_page(void){
  int sidRequested;
  const char *zName;
  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    return;
  }
  sidRequested = atoi(PD("sid","0"));
  if( sidRequested<=0 || !agent_chat_session_exists(sidRequested) ){
    agent_api_v1_emit_error("missing or unknown sid parameter", "unknown_session");
    return;
  }
  zName = PD("name", "");
  db_begin_write();
  db_unprotect(PROTECT_READONLY);
  agent_chat_session_rename(sidRequested, zName);
  db_end_transaction(0);
  CX("{\"api_version\":\"v1\",\"ok\":true,\"capabilities\":");
  agent_api_v1_emit_capabilities();
  CX(",\"session\":");
  agent_emit_history_object_json(sidRequested);
  CX("}\n");
}

/*
** WEBPAGE: agent-api-v1-session-name
**
** Fossil-native flat alias for the versioned session rename API.
*/
void agent_api_v1_session_name_flat_page(void){
  agent_api_v1_session_name_page();
}

/*
** WEBPAGE: agent-api/v1/events
**
** Versioned event stream/list API. Query parameters: sid, after.
*/
void agent_api_v1_events_page(void){
  int sidRequested;
  int sidCurrent;
  int afterAcid;
  int lastAcid;
  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    return;
  }
  sidRequested = atoi(PD("sid","0"));
  if( sidRequested<=0 || !agent_chat_session_exists(sidRequested) ){
    agent_api_v1_emit_error("missing or unknown sid parameter", "unknown_session");
    return;
  }
  sidCurrent = sidRequested;
  afterAcid = atoi(PD("after","0"));
  if( afterAcid<0 ) afterAcid = 0;
  CX("{\"api_version\":\"v1\",\"ok\":true,\"sid\":%d,\"after\":%d,\"capabilities\":",
     sidCurrent, afterAcid);
  agent_api_v1_emit_capabilities();
  CX(",\"events\":");
  agent_emit_events_array_json(sidCurrent, afterAcid, &lastAcid);
  CX(",\"last_acid\":%d,\"request\":", lastAcid);
  agent_emit_latest_request_json(sidCurrent);
  CX("}\n");
}

/*
** WEBPAGE: agent-api-v1-events
**
** Fossil-native flat alias for the versioned event list API.
*/
void agent_api_v1_events_flat_page(void){
  agent_api_v1_events_page();
}

/*
** WEBPAGE: agent-api/v1/capabilities
**
** Versioned capability discovery for the Fossil agent backend.
*/
void agent_api_v1_capabilities_page(void){
  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    return;
  }
  CX("{\"api_version\":\"v1\",\"ok\":true,\"capabilities\":");
  agent_api_v1_emit_capabilities();
  CX("}\n");
}

/*
** WEBPAGE: agent-api-v1-capabilities
**
** Fossil-native flat alias for capability discovery.
*/
void agent_api_v1_capabilities_flat_page(void){
  agent_api_v1_capabilities_page();
}

/*
** WEBPAGE: agent-api/v1/tools
**
** Versioned tool registry descriptor for Fossil's internal agent tooling.
*/
void agent_api_v1_tools_page(void){
  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    return;
  }
  CX("{\"api_version\":\"v1\",\"ok\":true,\"tools\":");
  agent_emit_tool_array_json();
  CX(",\"capabilities\":");
  agent_api_v1_emit_capabilities();
  CX("}\n");
}

/*
** WEBPAGE: agent-api-v1-tools
**
** Fossil-native flat alias for tool registry discovery.
*/
void agent_api_v1_tools_flat_page(void){
  agent_api_v1_tools_page();
}

/*
** WEBPAGE: agent-api/v1/requests/active
**
** Versioned active request discovery. Fossil currently exposes no resumable
** server-side request registry, so this returns an empty list.
*/
void agent_api_v1_requests_active_page(void){
  int sidRequested;
  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    return;
  }
  sidRequested = atoi(PD("sid","0"));
  if( sidRequested>0 && !agent_chat_session_exists(sidRequested) ){
    agent_api_v1_emit_error("missing or unknown sid parameter", "unknown_session");
    return;
  }
  CX("{\"api_version\":\"v1\",\"ok\":true,\"sid\":%d,\"request_ids\":",
     sidRequested>0 ? sidRequested : 0);
  agent_emit_active_request_ids_json(sidRequested>0 ? sidRequested : 0);
  CX(",\"capabilities\":");
  agent_api_v1_emit_capabilities();
  CX("}\n");
}

/*
** WEBPAGE: agent-api-v1-requests-active
**
** Fossil-native flat alias for active request discovery.
*/
void agent_api_v1_requests_active_flat_page(void){
  agent_api_v1_requests_active_page();
}

/*
** WEBPAGE: agent-api/v1/request/cancel
**
** Versioned request cancellation API. Explicitly unsupported for Fossil's
** current synchronous backend model.
*/
void agent_api_v1_request_cancel_page(void){
  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    return;
  }
  agent_api_v1_emit_error(
    "request cancellation is not supported by the Fossil backend",
    "unsupported"
  );
}

/*
** WEBPAGE: agent-api-v1-request-cancel
**
** Fossil-native flat alias for request cancellation.
*/
void agent_api_v1_request_cancel_flat_page(void){
  agent_api_v1_request_cancel_page();
}

/*
** WEBPAGE: agent-api/v1/approval/waiting
**
** Mark the latest request for a session as waiting for explicit approval.
*/
void agent_api_v1_approval_waiting_page(void){
  const char *zTool;
  int sid;
  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    return;
  }
  sid = atoi(PD("sid","0"));
  zTool = PD("tool", "");
  if( sid<=0 || !agent_chat_session_exists(sid) ){
    agent_api_v1_emit_error("missing or unknown sid parameter", "unknown_session");
    return;
  }
  db_begin_write();
  db_unprotect(PROTECT_READONLY);
  agent_request_set_latest_state(sid, "waiting-approval", 0);
  db_end_transaction(0);
  CX("{\"api_version\":\"v1\",\"ok\":true,\"approval\":{\"tool\":%!j,"
     "\"state\":\"waiting-approval\"},\"request\":", zTool);
  agent_emit_latest_request_json(sid);
  CX(",\"capabilities\":");
  agent_api_v1_emit_capabilities();
  CX("}\n");
}

/*
** WEBPAGE: agent-api-v1-approval-waiting
**
** Fossil-native flat alias for marking a request as waiting approval.
*/
void agent_api_v1_approval_waiting_flat_page(void){
  agent_api_v1_approval_waiting_page();
}

/*
** WEBPAGE: agent-api/v1/approval/apply
**
** Apply an explicitly approved tool action. This currently supports the
** built-in edit_file tool and records the action as a first-class request.
*/
void agent_api_v1_approval_apply_page(void){
  Blob result = BLOB_INITIALIZER;
  const char *zUser;
  const char *zTool;
  const char *zPath;
  const char *zReplace;
  const char *zWith;
  const char *zRequestIdParam;
  char *zRequestId = 0;
  char *zMeta = 0;
  char *zRowMeta = 0;
  int sid;
  int rid = 0;
  int acid;
  int rc;

  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    return;
  }
  sid = atoi(PD("sid","0"));
  zTool = PD("tool", "");
  zPath = PD("path", "");
  zReplace = PD("replace", "");
  zWith = PD("with", "");
  zRequestIdParam = PD("request_id", "");
  zUser = (g.zLogin && g.zLogin[0]) ? g.zLogin : "guest";
  if( sid<=0 || !agent_chat_session_exists(sid) ){
    agent_api_v1_emit_error("missing or unknown sid parameter", "unknown_session");
    return;
  }
  if( fossil_strcmp(zTool, "edit_file")!=0 ){
    agent_api_v1_emit_error("unsupported approval tool", "unsupported_tool");
    return;
  }
  if( zPath[0]==0 || zReplace[0]==0 ){
    agent_api_v1_emit_error("missing approval payload", "missing_parameter");
    return;
  }

  db_begin_write();
  db_unprotect(PROTECT_READONLY);
  if( fossil_strcmp(agent_chat_session_request_state(sid), "waiting-approval")==0 ){
    rid = agent_request_latest_rid(sid);
    zRequestId = mprintf("%s", agent_chat_session_request_id(sid));
    agent_request_set_state(rid, "running", 0);
  }else{
    rid = agent_request_create(sid, zRequestIdParam, "running");
    zRequestId = mprintf("%s", agent_chat_session_request_id(sid));
  }
  zMeta = mprintf(
    "{\"request_id\":%!j,\"tool\":\"edit_file\",\"phase\":\"request\"}",
    zRequestId
  );
  agent_chat_save_event(sid, zUser, "tool_request",
                        agent_chat_provider(), agent_chat_session_model(sid, ""),
                        zMeta, "Applying approved edit");
  fossil_free(zMeta);
  zMeta = 0;

  rc = agent_apply_edit_tool(zPath, zReplace, zWith, &result);
  zMeta = mprintf(
    "{\"request_id\":%!j,\"tool\":\"edit_file\",\"phase\":\"result\","
    "\"status\":%!j}",
    zRequestId,
    rc==0 ? "ok" : "error"
  );
  agent_chat_save_event(sid, zUser, "tool_result",
                        agent_chat_provider(), agent_chat_session_model(sid, ""),
                        zMeta, blob_str(&result));
  fossil_free(zMeta);
  zMeta = 0;

  acid = agent_chat_save(
    sid, zUser, "agent", rc==0 ? "reply" : "error",
    agent_chat_provider(), agent_chat_session_model(sid, ""),
    (zRowMeta = mprintf("{\"request_id\":%!j}", zRequestId)), blob_str(&result)
  );
  fossil_free(zRowMeta);
  agent_request_set_state(rid, rc==0 ? "finished" : "failed", acid);
  CX("{\"api_version\":\"v1\",\"ok\":%s,\"approval\":{\"tool\":\"edit_file\","
     "\"applied\":%s,\"message\":%!j},\"request\":",
     rc==0 ? "true" : "false",
     rc==0 ? "true" : "false",
     blob_str(&result));
  agent_emit_request_object_json(sid, zRequestId);
  CX(",\"capabilities\":");
  agent_api_v1_emit_capabilities();
  if( rc!=0 ){
    CX(",\"error\":%!j,\"error_code\":\"approval_failed\"", blob_str(&result));
  }
  CX("}\n");
  db_end_transaction(0);

  blob_reset(&result);
  fossil_free(zRequestId);
}

/*
** WEBPAGE: agent-api-v1-approval-apply
**
** Fossil-native flat alias for approval application.
*/
void agent_api_v1_approval_apply_flat_page(void){
  agent_api_v1_approval_apply_page();
}

/*
** WEBPAGE: agent-api/v1/session/delete
**
** Versioned session deletion API. Explicitly unsupported until Fossil has a
** contract-safe delete lifecycle for stored chat state.
*/
void agent_api_v1_session_delete_page(void){
  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    return;
  }
  agent_api_v1_emit_error(
    "session deletion is not supported by the Fossil backend",
    "unsupported"
  );
}

/*
** WEBPAGE: agent-api-v1-session-delete
**
** Fossil-native flat alias for session deletion.
*/
void agent_api_v1_session_delete_flat_page(void){
  agent_api_v1_session_delete_page();
}

/*
** WEBPAGE: agent-api/v1/session/fork
**
** Versioned session fork API. Explicitly unsupported until Fossil exposes a
** first-class branch/truncate chat lifecycle in the v1 contract.
*/
void agent_api_v1_session_fork_page(void){
  login_check_credentials();
  cgi_set_content_type("application/json");
  if( !g.perm.Read ){
    agent_api_v1_emit_error("missing read permissions or not logged in", 0);
    return;
  }
  agent_api_v1_emit_error(
    "session fork is not supported by the Fossil backend",
    "unsupported"
  );
}

/*
** WEBPAGE: agent-api-v1-session-fork
**
** Fossil-native flat alias for session fork.
*/
void agent_api_v1_session_fork_flat_page(void){
  agent_api_v1_session_fork_page();
}

/*
** WEBPAGE: agent-api/v1/chat
**
** Versioned chat submission API.
*/
void agent_api_v1_chat_page(void){
  agent_chat_page_impl(1);
}

/*
** WEBPAGE: agent-api-v1-chat
**
** Fossil-native flat alias for versioned chat submission.
*/
void agent_api_v1_chat_flat_page(void){
  agent_api_v1_chat_page();
}
