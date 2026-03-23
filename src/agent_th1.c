/*
** Copyright (c) 2026
**
** TH1 bridge for the Fossil agent stack.
*/
#include "config.h"
#include "agent.h"
#include "agent_internal.h"
#include "th.h"

int blob_write_to_file(Blob *pBlob, const char *zFilename);

void ai_schema_ensure(void);
int ai_is_enabled(void);
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
char *ai_note_related_nids(int nid, int limit);
void ai_note_link_upsert(int fromNid, int toNid, const char *zLinkType, double rWeight);
void ai_chat_eval_record(
  int sid,
  int acid,
  const char *zProvider,
  const char *zModel,
  const char *zKind,
  const char *zMsg
);

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

static int agent_asset_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Blob asset = BLOB_INITIALIZER;
  if( argc!=2 ) return Th_WrongNumArgs(interp, "agent_asset PATH");
  if( agent_load_asset(argv[1], &asset) ){
    Th_SetResult(interp, blob_str(&asset), blob_size(&asset));
  }else{
    Th_SetResult(interp, "", 0);
  }
  blob_reset(&asset);
  return TH_OK;
}

static int agent_tool_info_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  const AgentToolDef *pTool;
  char *zJson;
  if( argc!=2 ) return Th_WrongNumArgs(interp, "agent_tool_info TOOL_NAME");
  pTool = agent_tool_find(argv[1]);
  if( pTool==0 ){
    Th_SetResult(interp, "null", 4);
    return TH_OK;
  }
  zJson = mprintf(
    "{\"name\":%!j,\"description\":%!j,\"kind\":%!j,"
    "\"requires_confirmation\":%s,\"builtin\":%s}",
    pTool->zName, pTool->zDescription, pTool->zKind,
    pTool->bRequiresConfirm ? "true" : "false",
    pTool->bBuiltin ? "true" : "false"
  );
  Th_SetResult(interp, zJson, -1);
  fossil_free(zJson);
  return TH_OK;
}

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

static int agent_request_state_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int sid;
  int terminalAcid = 0;
  if( argc!=3 && argc!=4 ){
    return Th_WrongNumArgs(interp, "agent_request_state SID STATE ?TERMINAL_ACID?");
  }
  sid = atoi(argv[1]);
  if( argc==4 ){
    terminalAcid = atoi(argv[3]);
  }
  agent_request_set_latest_state(sid, argv[2], terminalAcid);
  Th_SetResultInt(interp, agent_request_latest_rid(sid));
  return TH_OK;
}

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

  {
    const char *zTool = argv[1];
    const AgentToolDef *pTool = agent_tool_find(zTool);
    if( pTool==0 ){
      blob_appendf(&out, "Error: Unknown tool %s", zTool);
      Th_SetResult(interp, blob_str(&out), blob_size(&out));
      blob_reset(&out);
      return TH_OK;
    }
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
      const char *zPath;
      const char *zReplace;
      const char *zWith;
      int bConfirmed;
      if( argc<6 ) return Th_WrongNumArgs(interp, "agent_mcp_call edit_file PATH EXPL REPLACE WITH CONFIRMED");
      zPath = argv[2];
      zReplace = argv[4];
      zWith = argv[5];
      bConfirmed = atoi(argv[6]);

      if( bConfirmed ){
        agent_apply_edit_tool(zPath, zReplace, zWith, &out);
      }else{
        blob_appendf(&out,
          "{\"type\":\"propose_edit\",\"tool\":\"edit_file\","
          "\"requires_confirmation\":true,"
          "\"path\":%!j,\"replace\":%!j,\"with\":%!j}",
          zPath, zReplace, zWith
        );
      }
    }
  }

  Th_SetResult(interp, blob_str(&out), blob_size(&out));
  blob_reset(&out);
  return TH_OK;
}

int agent_apply_edit_tool(
  const char *zPath,
  const char *zReplace,
  const char *zWith,
  Blob *pOut
){
  Blob content = BLOB_INITIALIZER;
  char *zOld;
  char *zPos;
  blob_zero(pOut);
  if( zPath==0 || zPath[0]==0 ){
    blob_appendf(pOut, "Error: Missing file path for edit.");
    return 1;
  }
  if( blob_read_from_file(&content, zPath, ExtFILE)<0 ){
    blob_appendf(pOut, "Error: Could not read file %s for editing.", zPath);
    return 1;
  }
  zOld = (char*)blob_str(&content);
  zPos = zReplace ? strstr(zOld, zReplace) : 0;
  if( zPos ){
    Blob next = BLOB_INITIALIZER;
    blob_append(&next, zOld, (int)(zPos - zOld));
    blob_append(&next, zWith ? zWith : "", -1);
    blob_append(&next, zPos + strlen(zReplace), -1);
    blob_write_to_file(&next, zPath);
    blob_appendf(pOut, "Successfully applied the edit to %s.", zPath);
    blob_reset(&next);
    blob_reset(&content);
    return 0;
  }else{
    blob_appendf(pOut,
      "Error: The target text to replace was not found in %s.", zPath
    );
    blob_reset(&content);
    return 1;
  }
}

static int agent_save_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Blob msg = BLOB_INITIALIZER;
  int sid, acid, i;
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

static int agent_save_event_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Blob msg = BLOB_INITIALIZER;
  int sid, i;
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

static int agent_save_reasoning_th1(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int sid;
  int acid;
  const char *zProvider;
  const char *zModel;
  const char *zMsg;
  Blob body = BLOB_INITIALIZER;
  char *zTitle = 0;
  char *zMeta = 0;
  int nid = 0;

  if( argc!=6 ){
    return Th_WrongNumArgs(interp, "agent_save_reasoning SID ACID PROVIDER MODEL MSG");
  }
  if( !ai_is_enabled() ) return TH_OK;
  sid = atoi(argv[1]);
  acid = atoi(argv[2]);
  zProvider = argv[3];
  zModel = argv[4];
  zMsg = argv[5];
  if( zMsg==0 || zMsg[0]==0 ) return TH_OK;

  ai_schema_ensure();
  blob_init(&body, zMsg, -1);
  zTitle = mprintf("Agent reasoning sid %d acid %d", sid, acid);
  zMeta = mprintf("{\"source\":\"agentchat\",\"sid\":%d,\"acid\":%d,"
                  "\"provider\":%!j,\"model\":%!j}",
                  sid, acid, zProvider ? zProvider : "", zModel ? zModel : "");
  nid = ai_note_create(
    0, zTitle, &body, "reasoning", 0, "agentchat", "raw", zMeta,
    0, 0, 0, 0, 0
  );
  fossil_free(zTitle);
  fossil_free(zMeta);
  blob_reset(&body);
  Th_SetResultInt(interp, nid);
  return TH_OK;
}

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
    {"agent_request_state", agent_request_state_th1, 0},
    {"agent_save",       agent_save_th1, 0},
    {"agent_save_event", agent_save_event_th1, 0},
    {"agent_save_reasoning", agent_save_reasoning_th1, 0},
    {"agent_config",     agent_config_th1, 0},
    {"agent_eval",       agent_eval_th1, 0},
    {"agent_mcp_call",   agent_mcp_call_th1, 0},
    {"agent_json_extract", agent_json_extract_th1, 0},
    {"agent_asset",        agent_asset_th1, 0},
    {"agent_tool_info",    agent_tool_info_th1, 0},
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
