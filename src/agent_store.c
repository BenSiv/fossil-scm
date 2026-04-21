/*
** Copyright (c) 2026
**
** Session and event persistence helpers for the Fossil agent stack.
*/
#include "config.h"
#include "agent.h"
#include "agent_internal.h"

int db_get_int(const char *zName, int dflt);

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
@ CREATE TABLE repository.agent_request(
@   rid INTEGER PRIMARY KEY AUTOINCREMENT,
@   sid INTEGER REFERENCES agentchat_session,
@   request_id TEXT,
@   state TEXT NOT NULL,
@   terminal_acid INTEGER,
@   reason TEXT,
@   ctime JULIANDAY DEFAULT (julianday('now')),
@   mtime JULIANDAY DEFAULT (julianday('now'))
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
    if( !db_table_exists("repository","agent_request") ){
      db_multi_exec(
        "CREATE TABLE repository.agent_request("
        "  rid INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  sid INTEGER REFERENCES agentchat_session,"
        "  request_id TEXT,"
        "  state TEXT NOT NULL,"
        "  terminal_acid INTEGER,"
        "  ctime JULIANDAY DEFAULT (julianday('now')),"
        "  mtime JULIANDAY DEFAULT (julianday('now'))"
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
    if( !db_table_has_column("repository","agent_request","terminal_acid") ){
      db_multi_exec("ALTER TABLE agent_request ADD COLUMN terminal_acid INTEGER");
    }
    if( !db_table_has_column("repository","agent_request","reason") ){
      db_multi_exec("ALTER TABLE agent_request ADD COLUMN reason TEXT");
    }
  }
}

int agent_request_create(int sid, const char *zRequestId, const char *zState){
  int rid;
  agent_chat_create_tables();
  db_multi_exec(
    "INSERT INTO agent_request(sid,request_id,state,terminal_acid,ctime,mtime)"
    " VALUES(%d,%Q,%Q,NULL,julianday('now'),julianday('now'))",
    sid,
    zRequestId && zRequestId[0] ? zRequestId : "",
    zState && zState[0] ? zState : "running"
  );
  rid = db_last_insert_rowid();
  if( zRequestId==0 || zRequestId[0]==0 ){
    db_multi_exec(
      "UPDATE agent_request"
      " SET request_id=printf('req-%%d', rid)"
      " WHERE rid=%d",
      rid
    );
  }
  return rid;
}

void agent_request_set_state(int rid, const char *zState, int terminalAcid, const char *zReason){
  if( rid<=0 ) return;
  db_multi_exec(
    "UPDATE agent_request"
    " SET state=%Q,"
    " terminal_acid=CASE WHEN %d>0 THEN %d ELSE terminal_acid END,"
    " reason=coalesce(%Q, reason),"
    " mtime=julianday('now')"
    " WHERE rid=%d",
    zState && zState[0] ? zState : "finished",
    terminalAcid,
    terminalAcid,
    zReason,
    rid
  );
}

int agent_request_latest_rid(int sid){
  if( sid<=0 || !db_table_exists("repository","agent_request") ) return 0;
  return db_int(0,
    "SELECT rid FROM agent_request"
    " WHERE sid=%d"
    " ORDER BY mtime DESC, rid DESC LIMIT 1",
    sid
  );
}

void agent_request_set_latest_state(int sid, const char *zState, int terminalAcid, const char *zReason){
  int rid = agent_request_latest_rid(sid);
  if( rid>0 ){
    agent_request_set_state(rid, zState, terminalAcid, zReason);
  }
}

int agent_chat_session_create(
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

void agent_chat_session_rename(int sid, const char *zTitle){
  Blob title = BLOB_INITIALIZER;
  if( sid<=0 || !agent_chat_session_exists(sid) ) return;
  if( zTitle==0 ) zTitle = "";
  blob_init(&title, zTitle, -1);
  blob_trim(&title);
  db_multi_exec(
    "UPDATE agentchat_session"
    " SET mtime=julianday('now'),"
    " title=%Q"
    " WHERE sid=%d",
    blob_size(&title)>0 ? blob_str(&title) : "New Chat",
    sid
  );
  blob_reset(&title);
}

int agent_chat_session_exists(int sid){
  return sid>0
    && db_table_exists("repository","agentchat_session")
    && db_exists("SELECT 1 FROM agentchat_session WHERE sid=%d", sid);
}

int agent_chat_latest_session(const char *zUser){
  if( !db_table_exists("repository","agentchat_session") ) return 0;
  return db_int(0,
    "SELECT sid FROM agentchat_session"
    " WHERE xfrom=%Q OR (%Q='' AND xfrom='')"
    " ORDER BY mtime DESC, sid DESC LIMIT 1",
    zUser ? zUser : "", zUser ? zUser : ""
  );
}

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

int agent_chat_save(
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
** Load the full message history for a session into the provided context.
** Limits history to the most recent 50 messages to keep context manageable.
*/
void agent_chat_session_context_load(int sid, AgentSessionContext *pCtx){
  Stmt q;
  int n = 0;
  int nLimit = db_get_int("agent-history-count", 50);
  if( sid<=0 ) return;
  db_prepare(&q,
    "SELECT role, msg FROM agentchat"
    " WHERE sid=%d"
    " ORDER BY acid ASC LIMIT %d",
    sid, nLimit
  );
  while( db_step(&q)==SQLITE_ROW ){
    n++;
  }
  sqlite3_reset(q.pStmt);
  if( n>0 ){
    pCtx->aMsg = fossil_malloc(sizeof(AgentMessage) * n);
    pCtx->nMsg = 0;
    while( db_step(&q)==SQLITE_ROW ){
      AgentMessage *pMsg = &pCtx->aMsg[pCtx->nMsg++];
      pMsg->zRole = fossil_strdup(db_column_text(&q, 0));
      pMsg->zContent = fossil_strdup(db_column_text(&q, 1));
      pMsg->nContent = (int)strlen(pMsg->zContent);
    }
  }
  db_finalize(&q);
  pCtx->zSystemPrompt = agent_config_get("system_prompt");
}

/*
** Free any dynamically allocated memory within the session context.
*/
void agent_chat_session_context_free(AgentSessionContext *pCtx){
  int i;
  if( pCtx->aMsg ){
    for(i=0; i<pCtx->nMsg; i++){
      fossil_free((char*)pCtx->aMsg[i].zRole);
      fossil_free((char*)pCtx->aMsg[i].zContent);
    }
    fossil_free(pCtx->aMsg);
  }
  fossil_free((char*)pCtx->zSystemPrompt);
  memset(pCtx, 0, sizeof(*pCtx));
}

void agent_chat_save_event(
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

const char *agent_chat_session_state(int sid){
  if( sid>0 && db_table_exists("repository","agent_request") ){
    const char *zReqState = db_text("",
      "SELECT state FROM agent_request WHERE sid=%d"
      " ORDER BY mtime DESC, rid DESC LIMIT 1",
      sid
    );
    if( zReqState && zReqState[0]
     && fossil_strcmp(zReqState, "finished")!=0
     && fossil_strcmp(zReqState, "reply")!=0
    ){
      return zReqState;
    }
  }
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
    "  WHEN role='system' AND kind IN ('tool','tool_request','tool_result') THEN 'tool'"
    "  WHEN role='system' AND kind='progress' THEN 'progress'"
    "  WHEN role='user' AND kind='prompt' THEN 'prompt'"
    "  ELSE coalesce(kind, role, '') END"
    " FROM agentchat WHERE sid=%d ORDER BY acid DESC LIMIT 1",
    sid
  );
}

const char *agent_chat_session_request_id(int sid){
  if( sid<=0 || !db_table_exists("repository","agent_request") ) return "";
  return db_text("",
    "SELECT coalesce(request_id,'') FROM agent_request WHERE sid=%d"
    " ORDER BY mtime DESC, rid DESC LIMIT 1",
    sid
  );
}

const char *agent_chat_session_request_state(int sid){
  if( sid<=0 || !db_table_exists("repository","agent_request") ) return "";
  return db_text("",
    "SELECT coalesce(state,'') FROM agent_request WHERE sid=%d"
    " ORDER BY mtime DESC, rid DESC LIMIT 1",
    sid
  );
}

int agent_chat_session_request_count(int sid){
  if( sid<=0 || !db_table_exists("repository","agent_request") ) return 0;
  return db_int(0, "SELECT count(*) FROM agent_request WHERE sid=%d", sid);
}

void agent_emit_request_object_json_to_blob(
  int sidCurrent,
  const char *zRequestId,
  Blob *pOut
){
  Stmt q;
  const char *zState;
  int isActive;
  int isTerminal;
  if( sidCurrent<=0 || !db_table_exists("repository","agent_request") ){
    blob_append(pOut, "null", 4);
    return;
  }
  if( zRequestId && zRequestId[0] ){
    db_prepare(&q,
      "SELECT rid, coalesce(request_id,''), coalesce(state,''),"
      "       coalesce(terminal_acid,0),"
      "       coalesce(datetime(ctime,toLocal()),''),"
      "       coalesce(datetime(mtime,toLocal()),''),"
      "       coalesce(reason,'')"
      "  FROM agent_request"
      " WHERE sid=%d AND request_id=%Q"
      " ORDER BY mtime DESC, rid DESC LIMIT 1",
      sidCurrent, zRequestId
    );
  }else{
    db_prepare(&q,
      "SELECT rid, coalesce(request_id,''), coalesce(state,''),"
      "       coalesce(terminal_acid,0),"
      "       coalesce(datetime(ctime,toLocal()),''),"
      "       coalesce(datetime(mtime,toLocal()),''),"
      "       coalesce(reason,'')"
      "  FROM agent_request"
      " WHERE sid=%d"
      " ORDER BY mtime DESC, rid DESC LIMIT 1",
      sidCurrent
    );
  }
  if( db_step(&q)==SQLITE_ROW ){
    zState = db_column_text(&q, 2);
    isActive = zState
      && (fossil_strcmp(zState, "queued")==0
       || fossil_strcmp(zState, "running")==0
       || fossil_strcmp(zState, "waiting-approval")==0);
    isTerminal = zState
      && (fossil_strcmp(zState, "finished")==0
       || fossil_strcmp(zState, "failed")==0
       || fossil_strcmp(zState, "cancelled")==0
       || fossil_strcmp(zState, "reply")==0
       || fossil_strcmp(zState, "error")==0);
    blob_appendf(pOut, "{\"rid\":%d,\"request_id\":%!j,\"state\":%!j,\"terminal_acid\":%d,"
                 "\"ctime\":%!j,\"mtime\":%!j,\"reason\":%!j,"
                 "\"is_active\":%s,\"is_terminal\":%s}",
                 db_column_int(&q, 0),
                 db_column_text(&q, 1),
                 zState,
                 db_column_int(&q, 3),
                 db_column_text(&q, 4),
                 db_column_text(&q, 5),
                 db_column_text(&q, 6),
                 isActive ? "true" : "false",
                 isTerminal ? "true" : "false");
  }else{
    blob_append(pOut, "null", 4);
  }
  db_finalize(&q);
}

void agent_emit_request_object_json(int sidCurrent, const char *zRequestId){
  Blob out = BLOB_INITIALIZER;
  agent_emit_request_object_json_to_blob(sidCurrent, zRequestId, &out);
  CX("%s", blob_str(&out));
  blob_reset(&out);
}

void agent_emit_latest_request_json(int sidCurrent){
  agent_emit_request_object_json(sidCurrent, 0);
}

int agent_chat_latest_terminal_acid(int sid){
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

int agent_chat_is_terminal_acid(int sid, int acid){
  return sid>0 && acid>0 && db_exists(
    "SELECT 1 FROM agentchat"
    " WHERE sid=%d AND acid=%d"
    "   AND role='agent'"
    "   AND kind IN ('reply','error')",
    sid, acid
  );
}

void agent_chat_render_sessions_to_blob(
  const char *zUser,
  int sidCurrent,
  Blob *pOut
){
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

static int agent_chat_meta_context_enabled(const char *zMeta){
  return zMeta && strstr(zMeta, "\"context\":true")!=0;
}

static int agent_chat_meta_retrieval_qid(const char *zMeta){
  const char *z;
  if( zMeta==0 ) return 0;
  z = strstr(zMeta, "\"retrieval_qid\":");
  if( z==0 ) return 0;
  z += 16;
  while( fossil_isspace(z[0]) ) z++;
  return atoi(z);
}

void agent_chat_render_history_to_blob(int sidCurrent, Blob *pOut){
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

void agent_emit_history_object_json(int sidCurrent){
  Stmt q;
  const char *zTitle = "New Chat";
  const char *zProvider = agent_chat_session_provider(sidCurrent, "");
  const char *zModel = agent_chat_session_model(sidCurrent, "");
  const char *zState = agent_chat_session_state(sidCurrent);
  const char *zReqId = agent_chat_session_request_id(sidCurrent);
  const char *zReqState = agent_chat_session_request_state(sidCurrent);
  char *zCtime = 0;
  char *zMtime = 0;
  int nMsg = 0;
  int nReq = 0;
  if( sidCurrent>0 && db_table_exists("repository","agentchat_session") ){
    zTitle = db_text("New Chat",
      "SELECT coalesce(nullif(title,''),'New Chat') FROM agentchat_session"
      " WHERE sid=%d",
      sidCurrent
    );
    zCtime = db_text(0,
      "SELECT datetime(ctime,toLocal()) FROM agentchat_session WHERE sid=%d",
      sidCurrent
    );
    zMtime = db_text(0,
      "SELECT datetime(mtime,toLocal()) FROM agentchat_session WHERE sid=%d",
      sidCurrent
    );
    nMsg = db_int(0, "SELECT count(*) FROM agentchat WHERE sid=%d", sidCurrent);
    nReq = agent_chat_session_request_count(sidCurrent);
  }
  CX("{\"sid\":%d,\"title\":%!j,\"provider\":%!j,\"model\":%!j,"
     "\"ctime\":%!j,\"mtime\":%!j,\"message_count\":%d,"
     "\"state\":%!j,\"request_count\":%d,\"last_request_id\":%!j,"
     "\"last_request_state\":%!j,\"request\":",
     sidCurrent, zTitle, zProvider, zModel,
     zCtime ? zCtime : "", zMtime ? zMtime : "", nMsg,
     zState ? zState : "", nReq, zReqId ? zReqId : "", zReqState ? zReqState : "");
  agent_emit_latest_request_json(sidCurrent);
  CX(",\"messages\":[");
  if( sidCurrent>0 && db_table_exists("repository","agentchat") ){
    int first = 1;
    if( db_table_exists("repository","ai_chat_eval") ){
      db_prepare(&q,
        "SELECT c.acid, c.role, c.kind, c.provider, c.model, c.meta, c.msg,"
        "       coalesce(e.user_feedback,''),"
        "       CASE"
        "         WHEN c.role='user' AND c.kind='prompt' THEN 'message'"
        "         WHEN c.role='agent' AND c.kind='reply' THEN 'message'"
        "         WHEN c.role='agent' AND c.kind='error' THEN 'error'"
        "         WHEN c.role='system' AND c.kind IN ('tool','tool_request') THEN 'tool_request'"
        "         WHEN c.role='system' AND c.kind='tool_result' THEN 'tool_result'"
        "         WHEN c.role='system' AND c.kind='progress' THEN 'progress'"
        "         WHEN c.role='system' AND c.kind='context' THEN 'context'"
        "         ELSE coalesce(c.kind, c.role, '')"
        "       END,"
        "       coalesce(json_extract(c.meta,'$.request_id'),''),"
        "       coalesce(json_extract(c.meta,'$.tool'),''),"
        "       coalesce(json_extract(c.meta,'$.phase'),'')"
        "  FROM agentchat AS c"
        "  LEFT JOIN ai_chat_eval AS e ON e.sid=c.sid AND e.acid=c.acid"
        " WHERE c.sid=%d"
        " ORDER BY c.acid ASC",
        sidCurrent
      );
    }else{
      db_prepare(&q,
        "SELECT acid, role, kind, provider, model, meta, msg, '',"
        "       CASE"
        "         WHEN role='user' AND kind='prompt' THEN 'message'"
        "         WHEN role='agent' AND kind='reply' THEN 'message'"
        "         WHEN role='agent' AND kind='error' THEN 'error'"
        "         WHEN role='system' AND kind IN ('tool','tool_request') THEN 'tool_request'"
        "         WHEN role='system' AND kind='tool_result' THEN 'tool_result'"
        "         WHEN role='system' AND kind='progress' THEN 'progress'"
        "         WHEN role='system' AND kind='context' THEN 'context'"
        "         ELSE coalesce(kind, role, '')"
        "       END,"
        "       coalesce(json_extract(meta,'$.request_id'),''),"
        "       coalesce(json_extract(meta,'$.tool'),''),"
        "       coalesce(json_extract(meta,'$.phase'),'')"
        " FROM agentchat WHERE sid=%d"
        " ORDER BY acid ASC",
        sidCurrent
      );
    }
    while( db_step(&q)==SQLITE_ROW ){
      CX("%s{\"acid\":%d,\"role\":%!j,\"kind\":%!j,\"provider\":%!j,"
         "\"model\":%!j,\"meta\":%!j,\"msg\":%!j,\"feedback\":%!j,"
         "\"event_type\":%!j,\"request_id\":%!j,\"tool_name\":%!j,"
         "\"tool_phase\":%!j,\"tool\":",
         first ? "" : ",",
         db_column_int(&q, 0),
         db_column_text(&q, 1),
         db_column_text(&q, 2),
         db_column_text(&q, 3),
         db_column_text(&q, 4),
         db_column_text(&q, 5),
         db_column_text(&q, 6),
         db_column_text(&q, 7),
         db_column_text(&q, 8),
         db_column_text(&q, 9),
         db_column_text(&q, 10),
         db_column_text(&q, 11));
      agent_emit_tool_json(db_column_text(&q, 10));
      CX(",\"is_terminal\":%s}",
         ((db_column_text(&q,1) && fossil_strcmp(db_column_text(&q,1),"agent")==0
           && db_column_text(&q,2) && (fossil_strcmp(db_column_text(&q,2),"reply")==0
                                    || fossil_strcmp(db_column_text(&q,2),"error")==0))
          ? "true" : "false"));
      first = 0;
    }
    db_finalize(&q);
  }
  CX("]}");
  fossil_free(zCtime);
  fossil_free(zMtime);
}

void agent_emit_history_json(int sidCurrent){
  agent_emit_history_object_json(sidCurrent);
  CX("\n");
}

void agent_emit_events_array_json(int sidCurrent, int afterAcid, int *pLastAcid){
  Stmt q;
  const char *zRole;
  const char *zKind;
  const char *zEventType;
  int isTerminal;
  int first = 1;
  int lastAcid = afterAcid;
  CX("[");
  if( sidCurrent>0 && db_table_exists("repository","agentchat") ){
    if( db_table_exists("repository","ai_chat_eval") ){
      db_prepare(&q,
        "SELECT c.acid, c.role, c.kind, c.provider, c.model, c.meta, c.msg,"
        "       coalesce(e.user_feedback,''),"
        "       CASE"
        "         WHEN c.role='user' AND c.kind='prompt' THEN 'message'"
        "         WHEN c.role='agent' AND c.kind='reply' THEN 'message'"
        "         WHEN c.role='agent' AND c.kind='error' THEN 'error'"
        "         WHEN c.role='system' AND c.kind IN ('tool','tool_request') THEN 'tool_request'"
        "         WHEN c.role='system' AND c.kind='tool_result' THEN 'tool_result'"
        "         WHEN c.role='system' AND c.kind='progress' THEN 'progress'"
        "         WHEN c.role='system' AND c.kind='context' THEN 'context'"
        "         ELSE coalesce(c.kind, c.role, '')"
        "       END,"
        "       coalesce(json_extract(c.meta,'$.request_id'),''),"
        "       coalesce(json_extract(c.meta,'$.tool'),''),"
        "       coalesce(json_extract(c.meta,'$.phase'),'')"
        "  FROM agentchat AS c"
        "  LEFT JOIN ai_chat_eval AS e ON e.sid=c.sid AND e.acid=c.acid"
        " WHERE c.sid=%d AND c.acid>%d"
        " ORDER BY c.acid ASC",
        sidCurrent, afterAcid
      );
    }else{
      db_prepare(&q,
        "SELECT acid, role, kind, provider, model, meta, msg, '',"
        "       CASE"
        "         WHEN role='user' AND kind='prompt' THEN 'message'"
        "         WHEN role='agent' AND kind='reply' THEN 'message'"
        "         WHEN role='agent' AND kind='error' THEN 'error'"
        "         WHEN role='system' AND kind IN ('tool','tool_request') THEN 'tool_request'"
        "         WHEN role='system' AND kind='tool_result' THEN 'tool_result'"
        "         WHEN role='system' AND kind='progress' THEN 'progress'"
        "         WHEN role='system' AND kind='context' THEN 'context'"
        "         ELSE coalesce(kind, role, '')"
        "       END,"
        "       coalesce(json_extract(meta,'$.request_id'),''),"
        "       coalesce(json_extract(meta,'$.tool'),''),"
        "       coalesce(json_extract(meta,'$.phase'),'')"
        " FROM agentchat WHERE sid=%d AND acid>%d"
        " ORDER BY acid ASC",
        sidCurrent, afterAcid
      );
    }
    while( db_step(&q)==SQLITE_ROW ){
      int acid = db_column_int(&q, 0);
      zRole = db_column_text(&q, 1);
      zKind = db_column_text(&q, 2);
      zEventType = db_column_text(&q, 8);
      isTerminal =
        (zRole && fossil_strcmp(zRole, "agent")==0
         && zKind && (fossil_strcmp(zKind, "reply")==0
                   || fossil_strcmp(zKind, "error")==0))
        || (zEventType && (fossil_strcmp(zEventType, "error")==0
                        || fossil_strcmp(zEventType, "finish")==0));
      CX("%s{\"acid\":%d,\"role\":%!j,\"kind\":%!j,\"provider\":%!j,"
       "\"model\":%!j,\"meta\":%!j,\"msg\":%!j,\"feedback\":%!j,"
         "\"event_type\":%!j,\"request_id\":%!j,\"tool_name\":%!j,"
         "\"tool_phase\":%!j,\"tool\":",
         first ? "" : ",",
         acid,
         zRole,
         zKind,
         db_column_text(&q, 3),
         db_column_text(&q, 4),
         db_column_text(&q, 5),
         db_column_text(&q, 6),
         db_column_text(&q, 7),
         db_column_text(&q, 8),
         db_column_text(&q, 9),
         db_column_text(&q, 10),
         db_column_text(&q, 11));
      agent_emit_tool_json(db_column_text(&q, 10));
      CX(",\"is_terminal\":%s}",
         isTerminal ? "true" : "false");
      if( acid>lastAcid ) lastAcid = acid;
      first = 0;
    }
    db_finalize(&q);
  }
  CX("]");
  if( pLastAcid ) *pLastAcid = lastAcid;
}

void agent_emit_events_json(int sidCurrent, int afterAcid){
  int lastAcid = afterAcid;
  CX("{\"sid\":%d,\"after\":%d,\"events\":", sidCurrent, afterAcid);
  agent_emit_events_array_json(sidCurrent, afterAcid, &lastAcid);
  CX(",\"last_acid\":%d,\"request\":", lastAcid);
  agent_emit_latest_request_json(sidCurrent);
  CX("}\n");
}

void agent_emit_active_request_ids_json(int sidCurrent){
  Stmt q;
  int first = 1;
  CX("[");
  if( sidCurrent>0 && db_table_exists("repository","agent_request") ){
    db_prepare(&q,
      "SELECT request_id FROM agent_request"
      " WHERE sid=%d"
      "   AND state IN ('queued','running','waiting-approval')"
      " ORDER BY mtime DESC, rid DESC",
      sidCurrent
    );
    while( db_step(&q)==SQLITE_ROW ){
      CX("%s%!j", first ? "" : ",", db_column_text(&q, 0));
      first = 0;
    }
    db_finalize(&q);
  }
  CX("]");
}

void agent_emit_session_array_json(const char *zUser){
  Stmt q;
  int first = 1;
  CX("[");
  if( db_table_exists("repository","agentchat_session") ){
    db_prepare(&q,
      "SELECT sid, coalesce(nullif(title,''),'New Chat'),"
      "       coalesce(nullif(provider,''),''),"
      "       coalesce(nullif(model,''),''),"
      "       datetime(ctime,toLocal()),"
      "       datetime(mtime,toLocal()),"
      "       (SELECT count(*) FROM agentchat AS c WHERE c.sid=agentchat_session.sid),"
      "       coalesce((SELECT state FROM agent_request AS r"
      "                 WHERE r.sid=agentchat_session.sid"
      "                 ORDER BY r.mtime DESC, r.rid DESC LIMIT 1),''),"
      "       coalesce((SELECT request_id FROM agent_request AS r"
      "                 WHERE r.sid=agentchat_session.sid"
      "                 ORDER BY r.mtime DESC, r.rid DESC LIMIT 1),''),"
      "       (SELECT count(*) FROM agent_request AS r WHERE r.sid=agentchat_session.sid)"
      "  FROM agentchat_session"
      " WHERE xfrom=%Q OR (%Q='' AND xfrom='')"
      " ORDER BY mtime DESC, sid DESC",
      zUser ? zUser : "", zUser ? zUser : ""
    );
    while( db_step(&q)==SQLITE_ROW ){
      CX("%s{\"sid\":%d,\"title\":%!j,\"provider\":%!j,\"model\":%!j,"
         "\"ctime\":%!j,\"mtime\":%!j,\"message_count\":%d,"
         "\"state\":%!j,\"last_request_id\":%!j,\"request_count\":%d}",
         first ? "" : ",",
         db_column_int(&q, 0),
         db_column_text(&q, 1),
         db_column_text(&q, 2),
         db_column_text(&q, 3),
         db_column_text(&q, 4),
         db_column_text(&q, 5),
         db_column_int(&q, 6),
         db_column_text(&q, 7),
         db_column_text(&q, 8),
         db_column_int(&q, 9));
      first = 0;
    }
    db_finalize(&q);
  }
  CX("]");
}

void agent_emit_session_list_json(const char *zUser){
  CX("{\"sessions\":");
  agent_emit_session_array_json(zUser);
  CX("}\n");
}
