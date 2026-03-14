/*
** Copyright (c) 2026
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License").
**
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
*/
#include "config.h"
#include "ai.h"

/* From file.c */
char *fossil_getenv(const char *zName);

/*
** SETTING: ai-enable width=5 default=0
**
** Enable or disable repository-backed AI features.
*/

#if INTERFACE
/* Enable flag */
int ai_is_enabled(void);

/* Ensure AI tables exist when AI features are enabled. */
void ai_schema_ensure(void);

/* Abort if AI features are disabled. */
void ai_require_enabled(void);

/* Record AI provenance for a commit if environment context is present. */
void ai_record_commit(int rid, const char *zComment);

/* Insert a data-pool note and return its nid. */
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

/* Start a retrieval event and return its qid. */
int ai_retrieval_begin(int contextId, const char *zQuery);

/* Record note retrieval, update reinforcement, and return the delta applied. */
double ai_note_record_retrieval(
  int qid,
  int nid,
  int rank,
  double score,
  double tierWeight
);

/* Run the post-retrieval evaluation loop for qid. */
void ai_retrieval_review(int qid);

/* Record a lightweight chat-answer evaluation row. */
void ai_chat_eval_record(
  int sid,
  int acid,
  const char *zProvider,
  const char *zModel,
  const char *zKind,
  const char *zMsg
);
void ai_chat_eval_feedback(int sid, int acid, const char *zFeedback);

/* CLI entry point */
void ai_cmd(void);

/* Register AI-related SQL functions */
void ai_add_sql_func(sqlite3 *db);
#endif

#include <math.h>

/*
** Implementation of vec_distance(A, B).
** A and B are BLOBs containing arrays of float32.
** Returns 1.0 - cosine_similarity(A, B).
*/
static void ai_vec_distance_sqlfunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const float *a, *b;
  int nA, nB;
  double dot = 0.0, magA = 0.0, magB = 0.0;
  int i, n;
  if( argc!=2 ){
    sqlite3_result_null(context);
    return;
  }
  if( sqlite3_value_type(argv[0])!=SQLITE_BLOB
   || sqlite3_value_type(argv[1])!=SQLITE_BLOB
  ){
    sqlite3_result_null(context);
    return;
  }
  a = (const float*)sqlite3_value_blob(argv[0]);
  nA = sqlite3_value_bytes(argv[0]) / (int)sizeof(float);
  b = (const float*)sqlite3_value_blob(argv[1]);
  nB = sqlite3_value_bytes(argv[1]) / (int)sizeof(float);
  n = nA < nB ? nA : nB;
  if( n==0 ){
    sqlite3_result_null(context);
    return;
  }
  for(i=0; i<n; i++){
    dot += a[i] * b[i];
    magA += a[i] * a[i];
    magB += b[i] * b[i];
  }
  if( magA<=0.0 || magB<=0.0 ){
    sqlite3_result_double(context, 1.0);
  }else{
    double sim = dot / (sqrt(magA) * sqrt(magB));
    if( sim>1.0 ) sim = 1.0;
    if( sim<-1.0 ) sim = -1.0;
    sqlite3_result_double(context, 1.0 - sim);
  }
}

/*
** Return the default process level label for a tier.
*/
static const char *ai_process_level_for_tier(int tier){
  switch( tier ){
    case 3: return "atomic";
    case 2: return "curated";
    case 1: return "grouped";
    default: return "raw";
  }
}

/*
** Return the retrieval tier bonus used during weighting.
*/
static double ai_tier_weight_value(int tier){
  switch( tier ){
    case 3: return 0.35;
    case 2: return 0.20;
    case 1: return 0.10;
    default: return 0.0;
  }
}

/*
** Return true if zTitle is absent or too generic to be useful.
*/
static int ai_title_is_generic(const char *zTitle){
  if( zTitle==0 ) return 1;
  while( fossil_isspace(zTitle[0]) ) zTitle++;
  if( zTitle[0]==0 ) return 1;
  return fossil_stricmp(zTitle, "note")==0
      || fossil_stricmp(zTitle, "untitled note")==0
      || fossil_stricmp(zTitle, "manual note")==0;
}

/*
** Guess a title from the first non-empty line of zBody.
*/
static char *ai_guess_title_from_text(const char *zBody){
  int n;
  const char *zStart;
  if( zBody==0 ) return mprintf("Untitled note");
  while( fossil_isspace(zBody[0]) ) zBody++;
  while( zBody[0]=='#' || zBody[0]=='-' || zBody[0]=='*' || zBody[0]=='>' ){
    zBody++;
    while( fossil_isspace(zBody[0]) ) zBody++;
  }
  zStart = zBody;
  while( zBody[0] && zBody[0]!='\n' && zBody[0]!='\r' ){
    zBody++;
  }
  n = (int)(zBody - zStart);
  while( n>0 && fossil_isspace(zStart[n-1]) ) n--;
  if( n<=0 ) return mprintf("Untitled note");
  if( n>72 ){
    int i = 72;
    while( i>24 && !fossil_isspace(zStart[i]) ) i--;
    if( i>24 ) n = i;
    else n = 72;
  }
  return mprintf("%.*s", n, zStart);
}

/*
** Compute the content hash used for exact duplicate detection.
*/
static void ai_note_hash(Blob *pBody, Blob *pHashOut){
  blob_zero(pHashOut);
  sha1sum_blob(pBody, pHashOut);
}

/*
** Create or bump a link between two notes.
*/
static void ai_note_link_upsert(
  int fromNid,
  int toNid,
  const char *zLinkType,
  double rWeight
){
  if( fromNid<=0 || toNid<=0 || fromNid==toNid || zLinkType==0 ){
    return;
  }
  db_multi_exec(
    "INSERT INTO repository.ai_note_link("
    "  from_nid,to_nid,link_type,weight,updated_at"
    ") VALUES(%d,%d,%Q,%.17g,julianday('now'))"
    " ON CONFLICT(from_nid,to_nid,link_type) DO UPDATE SET"
    "   weight=repository.ai_note_link.weight+excluded.weight,"
    "   updated_at=excluded.updated_at;",
    fromNid, toNid, zLinkType, rWeight
  );
}

/*
** Return a simple atomicity classification for zBody.
*/
static const char *ai_atomicity_status(const char *zBody){
  int nHeading = 0;
  int nParagraph = 0;
  int bInText = 0;
  int i;
  if( zBody==0 || zBody[0]==0 ) return "thin";
  for(i=0; zBody[i]; i++){
    if( (i==0 || zBody[i-1]=='\n') && zBody[i]=='#' ) nHeading++;
    if( zBody[i]=='\n' ){
      if( zBody[i+1]=='\n' ) bInText = 0;
    }else if( !fossil_isspace(zBody[i]) ){
      if( !bInText ){
        nParagraph++;
        bInText = 1;
      }
    }
  }
  if( nHeading>1 || nParagraph>6 ) return "needs-split";
  if( nParagraph<=1 && (int)strlen(zBody)<64 ) return "thin";
  return "ok";
}

/*
** Insert a single review row.
*/
static void ai_review_insert(
  int qid,
  int nid,
  const char *zAtomicity,
  const char *zConnectivity,
  const char *zDuplication,
  const char *zTitleStatus,
  const char *zMetadataStatus
){
  char *zSummary = mprintf(
    "atomicity=%s; connectivity=%s; duplication=%s; title=%s; metadata=%s",
    zAtomicity, zConnectivity, zDuplication, zTitleStatus, zMetadataStatus
  );
  Stmt q;
  db_prepare(&q,
    "INSERT INTO repository.ai_review("
    "  qid,nid,atomicity_status,connectivity_status,duplication_status,"
    "  title_status,metadata_status,action_summary,created_at"
    ") VALUES("
    "  :qid,:nid,:atomicity,:connectivity,:duplication,"
    "  :title_status,:metadata_status,:summary,julianday('now')"
    ");"
  );
  db_bind_int(&q, ":qid", qid);
  db_bind_int(&q, ":nid", nid);
  db_bind_text(&q, ":atomicity", zAtomicity);
  db_bind_text(&q, ":connectivity", zConnectivity);
  db_bind_text(&q, ":duplication", zDuplication);
  db_bind_text(&q, ":title_status", zTitleStatus);
  db_bind_text(&q, ":metadata_status", zMetadataStatus);
  db_bind_text(&q, ":summary", zSummary);
  db_step(&q);
  db_finalize(&q);
  fossil_free(zSummary);
}

/*
** Record user feedback for an existing chat-answer evaluation row.
*/
void ai_chat_eval_feedback(int sid, int acid, const char *zFeedback){
  if( !ai_is_enabled() ) return;
  if( sid<=0 || acid<=0 || zFeedback==0 || zFeedback[0]==0 ) return;
  ai_schema_ensure();
  db_multi_exec(
    "UPDATE repository.ai_chat_eval"
    " SET user_feedback=%Q,"
    "     feedback_at=julianday('now')"
    " WHERE sid=%d AND acid=%d",
    zFeedback, sid, acid
  );
}

/*
** Return non-zero if zMsg appears to expose visible reasoning text rather
** than only a final answer.
*/
static int ai_chat_eval_has_visible_reasoning(const char *zMsg){
  if( zMsg==0 || zMsg[0]==0 ) return 0;
  return strstr(zMsg, "Thinking...")!=0
      || strstr(zMsg, "<think>")!=0
      || strstr(zMsg, "</think>")!=0
      || fossil_strnicmp(zMsg, "thinking:", 9)==0;
}

/*
** Record one chat-answer evaluation row if AI features are enabled.
*/
void ai_chat_eval_record(
  int sid,
  int acid,
  const char *zProvider,
  const char *zModel,
  const char *zKind,
  const char *zMsg
){
  const char *zReplyKind;
  const char *zQuality;
  const char *zReasoning;
  char *zSummary;
  Stmt q;
  if( !ai_is_enabled() ) return;
  ai_schema_ensure();
  if( zKind && fossil_strcmp(zKind, "error")==0 ){
    zReplyKind = "error";
    zQuality = "error";
    zReasoning = "none";
  }else if( ai_chat_eval_has_visible_reasoning(zMsg) ){
    zReplyKind = "reasoning-visible";
    zQuality = "review";
    zReasoning = "visible";
  }else if( zMsg && zMsg[0] ){
    zReplyKind = "final";
    zQuality = "ok";
    zReasoning = "none";
  }else{
    zReplyKind = "empty";
    zQuality = "empty";
    zReasoning = "none";
  }
  zSummary = mprintf("reply_kind=%s; quality=%s; reasoning=%s",
                     zReplyKind, zQuality, zReasoning);
  db_prepare(&q,
    "INSERT INTO repository.ai_chat_eval("
    " sid,acid,provider,model,reply_kind,quality_status,reasoning_status,"
    " action_summary,created_at"
    ") VALUES("
    " :sid,:acid,:provider,:model,:reply_kind,:quality,:reasoning,"
    " :summary,julianday('now'))"
  );
  db_bind_int(&q, ":sid", sid);
  db_bind_int(&q, ":acid", acid);
  db_bind_text(&q, ":provider", zProvider ? zProvider : "");
  db_bind_text(&q, ":model", zModel ? zModel : "");
  db_bind_text(&q, ":reply_kind", zReplyKind);
  db_bind_text(&q, ":quality", zQuality);
  db_bind_text(&q, ":reasoning", zReasoning);
  db_bind_text(&q, ":summary", zSummary);
  db_step(&q);
  db_finalize(&q);
  fossil_free(zSummary);
}

/*
** Register AI-related SQL functions with the database connection.
*/
void ai_add_sql_func(sqlite3 *db){
  sqlite3_create_function(
    db, "vec_distance", 2,
    SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
    0, ai_vec_distance_sqlfunc, 0, 0
  );
}

int ai_is_enabled(void){
  return db_get_boolean("ai-enable", 0);
}

void ai_require_enabled(void){
  if( !ai_is_enabled() ){
    fossil_fatal("ai is disabled; run 'fossil ai enable' first");
  }
  ai_schema_ensure();
}

void ai_schema_ensure(void){
  if( !ai_is_enabled() ) return;

  db_multi_exec(
    "CREATE TABLE IF NOT EXISTS repository.ai_note("
    "  nid INTEGER PRIMARY KEY,"
    "  tier INTEGER DEFAULT 0,"
    "  title TEXT,"
    "  body TEXT,"
    "  source_type TEXT,"
    "  source_id INTEGER,"
    "  source_ref TEXT,"
    "  process_level TEXT,"
    "  metadata TEXT,"
    "  heat REAL DEFAULT 1.0,"
    "  retrieval_count INTEGER DEFAULT 0,"
    "  last_retrieved_at TEXT,"
    "  content_hash TEXT,"
    "  duplicate_of INTEGER,"
    "  merged_into INTEGER,"
    "  created_at DATETIME DEFAULT (julianday('now')),"
    "  updated_at DATETIME DEFAULT (julianday('now'))"
    ");"
  );
  db_multi_exec(
    "CREATE TABLE IF NOT EXISTS repository.ai_context("
    "  cid INTEGER PRIMARY KEY,"
    "  rid INTEGER,"
    "  prompt TEXT,"
    "  rationale TEXT,"
    "  reasoning_note_id INTEGER,"
    "  model_id TEXT,"
    "  token_in INTEGER,"
    "  token_out INTEGER,"
    "  created_at DATETIME DEFAULT (julianday('now')),"
    "  tags TEXT"
    ");"
  );

  db_multi_exec(
    "CREATE TABLE IF NOT EXISTS repository.ai_note_link("
    "  from_nid INTEGER,"
    "  to_nid INTEGER,"
    "  link_type TEXT,"
    "  weight REAL DEFAULT 1.0,"
    "  updated_at DATETIME DEFAULT (julianday('now'))"
    ");"
    "CREATE TABLE IF NOT EXISTS repository.ai_retrieval("
    "  qid INTEGER PRIMARY KEY,"
    "  context_id INTEGER,"
    "  query_text TEXT,"
    "  created_at DATETIME DEFAULT (julianday('now'))"
    ");"
    "CREATE TABLE IF NOT EXISTS repository.ai_retrieval_note("
    "  qid INTEGER,"
    "  nid INTEGER,"
    "  rank INTEGER,"
    "  score REAL,"
    "  tier_weight REAL,"
    "  reinforcement_delta REAL"
    ");"
    "CREATE TABLE IF NOT EXISTS repository.ai_review("
    "  review_id INTEGER PRIMARY KEY,"
    "  qid INTEGER,"
    "  nid INTEGER,"
    "  atomicity_status TEXT,"
    "  connectivity_status TEXT,"
    "  duplication_status TEXT,"
    "  title_status TEXT,"
    "  metadata_status TEXT,"
    "  action_summary TEXT,"
    "  created_at DATETIME DEFAULT (julianday('now'))"
    ");"
    "CREATE TABLE IF NOT EXISTS repository.ai_chat_eval("
    "  eval_id INTEGER PRIMARY KEY,"
    "  sid INTEGER,"
    "  acid INTEGER,"
    "  provider TEXT,"
    "  model TEXT,"
    "  reply_kind TEXT,"
    "  quality_status TEXT,"
    "  reasoning_status TEXT,"
    "  user_feedback TEXT,"
    "  feedback_at DATETIME,"
    "  action_summary TEXT,"
    "  created_at DATETIME DEFAULT (julianday('now'))"
    ");"
  );

  db_multi_exec(
    "CREATE TABLE IF NOT EXISTS repository.ai_vector("
    "  vid INTEGER PRIMARY KEY,"
    "  source_type TEXT,"
    "  source_id INTEGER,"
    "  dim INTEGER,"
    "  vector BLOB"
    ");"
    "CREATE TABLE IF NOT EXISTS repository.ai_policy("
    "  key TEXT PRIMARY KEY,"
    "  value TEXT,"
    "  updated_at DATETIME DEFAULT (julianday('now'))"
    ");"
  );

  if( !db_table_has_column("repository","ai_context","reasoning_note_id") ){
    db_multi_exec(
      "ALTER TABLE repository.ai_context ADD COLUMN reasoning_note_id INTEGER;"
    );
  }
  if( !db_table_has_column("repository","ai_note","source_ref") ){
    db_multi_exec("ALTER TABLE repository.ai_note ADD COLUMN source_ref TEXT;");
  }
  if( !db_table_has_column("repository","ai_note","process_level") ){
    db_multi_exec(
      "ALTER TABLE repository.ai_note ADD COLUMN process_level TEXT;"
    );
  }
  if( !db_table_has_column("repository","ai_note","metadata") ){
    db_multi_exec("ALTER TABLE repository.ai_note ADD COLUMN metadata TEXT;");
  }
  if( !db_table_has_column("repository","ai_note","retrieval_count") ){
    db_multi_exec(
      "ALTER TABLE repository.ai_note ADD COLUMN retrieval_count"
      " INTEGER DEFAULT 0;"
    );
  }
  if( !db_table_has_column("repository","ai_note","last_retrieved_at") ){
    db_multi_exec(
      "ALTER TABLE repository.ai_note ADD COLUMN last_retrieved_at TEXT;"
    );
  }
  if( !db_table_has_column("repository","ai_note","content_hash") ){
    db_multi_exec("ALTER TABLE repository.ai_note ADD COLUMN content_hash TEXT;");
  }
  if( !db_table_has_column("repository","ai_note","duplicate_of") ){
    db_multi_exec("ALTER TABLE repository.ai_note ADD COLUMN duplicate_of INTEGER;");
  }
  if( !db_table_has_column("repository","ai_note","merged_into") ){
    db_multi_exec("ALTER TABLE repository.ai_note ADD COLUMN merged_into INTEGER;");
  }
  if( !db_table_exists("repository","ai_chat_eval") ){
    db_multi_exec(
      "CREATE TABLE repository.ai_chat_eval("
      "  eval_id INTEGER PRIMARY KEY,"
      "  sid INTEGER,"
      "  acid INTEGER,"
      "  provider TEXT,"
      "  model TEXT,"
      "  reply_kind TEXT,"
      "  quality_status TEXT,"
      "  reasoning_status TEXT,"
      "  user_feedback TEXT,"
      "  feedback_at DATETIME,"
      "  action_summary TEXT,"
      "  created_at DATETIME DEFAULT (julianday('now'))"
      ");"
    );
  }
  if( !db_table_has_column("repository","ai_chat_eval","user_feedback") ){
    db_multi_exec(
      "ALTER TABLE repository.ai_chat_eval ADD COLUMN user_feedback TEXT;"
    );
  }
  if( !db_table_has_column("repository","ai_chat_eval","feedback_at") ){
    db_multi_exec(
      "ALTER TABLE repository.ai_chat_eval ADD COLUMN feedback_at DATETIME;"
    );
  }

  db_multi_exec(
    "CREATE INDEX IF NOT EXISTS repository.ai_note_i1"
    " ON ai_note(tier, heat DESC, retrieval_count DESC);"
    "CREATE INDEX IF NOT EXISTS repository.ai_note_i2"
    " ON ai_note(content_hash);"
    "CREATE INDEX IF NOT EXISTS repository.ai_context_i1"
    " ON ai_context(rid);"
    "CREATE INDEX IF NOT EXISTS repository.ai_context_i2"
    " ON ai_context(reasoning_note_id);"
    "CREATE UNIQUE INDEX IF NOT EXISTS repository.ai_note_link_u1"
    " ON ai_note_link(from_nid, to_nid, link_type);"
    "CREATE INDEX IF NOT EXISTS repository.ai_note_link_i2"
    " ON ai_note_link(to_nid, link_type);"
    "CREATE INDEX IF NOT EXISTS repository.ai_retrieval_i1"
    " ON ai_retrieval(created_at);"
    "CREATE UNIQUE INDEX IF NOT EXISTS repository.ai_retrieval_note_u1"
    " ON ai_retrieval_note(qid, nid);"
    "CREATE INDEX IF NOT EXISTS repository.ai_retrieval_note_i2"
    " ON ai_retrieval_note(nid, qid);"
    "CREATE INDEX IF NOT EXISTS repository.ai_review_i1"
    " ON ai_review(qid, nid);"
    "CREATE INDEX IF NOT EXISTS repository.ai_chat_eval_i1"
    " ON ai_chat_eval(sid, acid);"
    "CREATE INDEX IF NOT EXISTS repository.ai_vector_i1"
    " ON ai_vector(source_type, source_id);"
  );
}

int ai_note_create(
  int tier,
  const char *zTitle,
  Blob *pBody,
  const char *zSourceType,
  int sourceId,
  const char *zSourceRef,
  const char *zProcessLevel,
  const char *zMetadata
){
  Blob hash = BLOB_INITIALIZER;
  Stmt q;
  char *zDerivedTitle = 0;
  char *zDerivedMeta = 0;
  int nid;

  ai_require_enabled();
  if( tier<0 ) tier = 0;
  if( tier>3 ) tier = 3;
  if( pBody==0 ){
    fossil_fatal("missing note body");
  }
  blob_trim(pBody);
  if( blob_size(pBody)==0 ){
    fossil_fatal("empty note body");
  }
  if( zSourceType==0 || zSourceType[0]==0 ){
    zSourceType = "manual";
  }
  if( zProcessLevel==0 || zProcessLevel[0]==0 ){
    zProcessLevel = ai_process_level_for_tier(tier);
  }
  if( ai_title_is_generic(zTitle) ){
    zDerivedTitle = ai_guess_title_from_text(blob_str(pBody));
    zTitle = zDerivedTitle;
  }
  if( zMetadata==0 || zMetadata[0]==0 ){
    zDerivedMeta = mprintf(
      "{\"process_level\":\"%s\",\"source_type\":\"%s\"}",
      zProcessLevel, zSourceType
    );
    zMetadata = zDerivedMeta;
  }
  ai_note_hash(pBody, &hash);
  db_prepare(&q,
    "INSERT INTO repository.ai_note("
    "  tier,title,body,source_type,source_id,source_ref,process_level,"
    "  metadata,heat,retrieval_count,last_retrieved_at,content_hash,"
    "  duplicate_of,merged_into,created_at,updated_at"
    ") VALUES("
    "  :tier,:title,:body,:source_type,:source_id,:source_ref,:process_level,"
    "  :metadata,1.0,0,NULL,:content_hash,NULL,NULL,"
    "  julianday('now'),julianday('now')"
    ");"
  );
  db_bind_int(&q, ":tier", tier);
  db_bind_text(&q, ":title", zTitle);
  db_bind_str(&q, ":body", pBody);
  db_bind_text(&q, ":source_type", zSourceType);
  if( sourceId>0 ) db_bind_int(&q, ":source_id", sourceId);
  else db_bind_null(&q, ":source_id");
  if( zSourceRef && zSourceRef[0] ) db_bind_text(&q, ":source_ref", zSourceRef);
  else db_bind_null(&q, ":source_ref");
  db_bind_text(&q, ":process_level", zProcessLevel);
  db_bind_text(&q, ":metadata", zMetadata);
  db_bind_str(&q, ":content_hash", &hash);
  db_step(&q);
  db_finalize(&q);
  nid = db_last_insert_rowid();
  blob_reset(&hash);
  fossil_free(zDerivedTitle);
  fossil_free(zDerivedMeta);
  return nid;
}

int ai_retrieval_begin(int contextId, const char *zQuery){
  Stmt q;
  ai_require_enabled();
  db_prepare(&q,
    "INSERT INTO repository.ai_retrieval(context_id,query_text,created_at)"
    " VALUES(:context_id,:query_text,julianday('now'));"
  );
  if( contextId>0 ) db_bind_int(&q, ":context_id", contextId);
  else db_bind_null(&q, ":context_id");
  if( zQuery && zQuery[0] ) db_bind_text(&q, ":query_text", zQuery);
  else db_bind_null(&q, ":query_text");
  db_step(&q);
  db_finalize(&q);
  return db_last_insert_rowid();
}

double ai_note_record_retrieval(
  int qid,
  int nid,
  int rank,
  double score,
  double tierWeight
){
  double rDelta;
  if( qid<=0 || nid<=0 ) return 0.0;
  ai_require_enabled();
  rDelta = 0.15 + tierWeight;
  db_multi_exec(
    "UPDATE repository.ai_note"
    "   SET retrieval_count=coalesce(retrieval_count,0)+1,"
    "       heat=coalesce(heat,1.0)+%.17g,"
    "       last_retrieved_at=julianday('now'),"
    "       updated_at=julianday('now')"
    " WHERE nid=%d;",
    rDelta, nid
  );
  db_multi_exec(
    "INSERT INTO repository.ai_retrieval_note("
    "  qid,nid,rank,score,tier_weight,reinforcement_delta"
    ") VALUES(%d,%d,%d,%.17g,%.17g,%.17g)"
    " ON CONFLICT(qid,nid) DO UPDATE SET"
    "   rank=excluded.rank,"
    "   score=excluded.score,"
    "   tier_weight=excluded.tier_weight,"
    "   reinforcement_delta=excluded.reinforcement_delta;",
    qid, nid, rank, score, tierWeight, rDelta
  );
  return rDelta;
}

void ai_retrieval_review(int qid){
  Stmt q;
  int nPeers;
  if( qid<=0 ) return;
  ai_require_enabled();

  db_begin_transaction();
  db_multi_exec(
    "INSERT INTO repository.ai_note_link("
    "  from_nid,to_nid,link_type,weight,updated_at"
    ")"
    " SELECT a.nid, b.nid, 'co_retrieved', 1.0, julianday('now')"
    "   FROM repository.ai_retrieval_note a, repository.ai_retrieval_note b"
    "  WHERE a.qid=%d"
    "    AND b.qid=%d"
    "    AND a.nid<b.nid"
    " ON CONFLICT(from_nid,to_nid,link_type) DO UPDATE SET"
    "   weight=repository.ai_note_link.weight+1.0,"
    "   updated_at=excluded.updated_at;",
    qid, qid
  );
  nPeers = db_int(0,
    "SELECT count(*)-1 FROM repository.ai_retrieval_note WHERE qid=%d",
    qid
  );
  if( nPeers<0 ) nPeers = 0;

  db_prepare(&q,
    "SELECT n.nid, coalesce(n.title,''), coalesce(n.body,''),"
    "       coalesce(n.tier,0), coalesce(n.process_level,''),"
    "       coalesce(n.metadata,''), coalesce(n.content_hash,'')"
    "  FROM repository.ai_note n"
    "  JOIN repository.ai_retrieval_note r ON r.nid=n.nid"
    " WHERE r.qid=%d"
    " ORDER BY r.rank ASC, n.nid ASC",
    qid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int nid = db_column_int(&q, 0);
    const char *zTitle = db_column_text(&q, 1);
    const char *zBody = db_column_text(&q, 2);
    int tier = db_column_int(&q, 3);
    const char *zProcess = db_column_text(&q, 4);
    const char *zMetadata = db_column_text(&q, 5);
    const char *zHash = db_column_text(&q, 6);
    const char *zAtomicity = ai_atomicity_status(zBody);
    char *zConnectivity = mprintf("linked-%d", nPeers);
    char *zDuplication = 0;
    char *zTitleStatus = 0;
    char *zMetadataStatus = 0;
    int canonicalNid = 0;

    if( zHash && zHash[0] ){
      canonicalNid = db_int(0,
        "SELECT min(nid) FROM repository.ai_note"
        " WHERE content_hash=%Q",
        zHash
      );
    }
    if( canonicalNid>0 && canonicalNid!=nid ){
      db_multi_exec(
        "UPDATE repository.ai_note"
        "   SET duplicate_of=%d, merged_into=%d, updated_at=julianday('now')"
        " WHERE nid=%d;",
        canonicalNid, canonicalNid, nid
      );
      ai_note_link_upsert(nid, canonicalNid, "duplicate", 1.0);
      zDuplication = mprintf("duplicate-of-%d", canonicalNid);
    }else{
      zDuplication = mprintf("unique");
    }

    if( ai_title_is_generic(zTitle) ){
      char *zNewTitle = ai_guess_title_from_text(zBody);
      if( fossil_strcmp(zNewTitle, zTitle)!=0 ){
        db_multi_exec(
          "UPDATE repository.ai_note"
          "   SET title=%Q, updated_at=julianday('now')"
          " WHERE nid=%d;",
          zNewTitle, nid
        );
        db_multi_exec(
          "DELETE FROM repository.ai_vector"
          " WHERE source_type='note' AND source_id=%d;",
          nid
        );
        zTitleStatus = mprintf("retitled");
      }else{
        zTitleStatus = mprintf("ok");
      }
      fossil_free(zNewTitle);
    }else{
      zTitleStatus = mprintf("ok");
    }

    if( zProcess==0 || zProcess[0]==0
     || fossil_strcmp(zProcess, ai_process_level_for_tier(tier))!=0
     || zMetadata==0 || zMetadata[0]==0
    ){
      char *zMeta = 0;
      if( zMetadata==0 || zMetadata[0]==0 ){
        zMeta = mprintf(
          "{\"process_level\":\"%s\"}",
          ai_process_level_for_tier(tier)
        );
      }
      db_multi_exec(
        "UPDATE repository.ai_note"
        "   SET process_level=%Q,"
        "       metadata=coalesce(%Q, metadata),"
        "       updated_at=julianday('now')"
        " WHERE nid=%d;",
        ai_process_level_for_tier(tier), zMeta, nid
      );
      zMetadataStatus = mprintf("updated");
      fossil_free(zMeta);
    }else{
      zMetadataStatus = mprintf("ok");
    }

    ai_review_insert(
      qid, nid, zAtomicity, zConnectivity, zDuplication,
      zTitleStatus, zMetadataStatus
    );
    fossil_free(zConnectivity);
    fossil_free(zDuplication);
    fossil_free(zTitleStatus);
    fossil_free(zMetadataStatus);
  }
  db_finalize(&q);
  db_end_transaction(0);
}

void ai_record_commit(int rid, const char *zComment){
  const char *zPrompt = 0;
  const char *zRationale = 0;
  const char *zReasoning = 0;
  const char *zModel = 0;
  const char *zTags = 0;
  int reasoningNoteId = 0;
  Stmt q;

  if( !ai_is_enabled() ) return;

  zPrompt = fossil_getenv("FOSSIL_AI_PROMPT");
  zRationale = fossil_getenv("FOSSIL_AI_RATIONALE");
  zReasoning = fossil_getenv("FOSSIL_AI_REASONING");
  zModel = fossil_getenv("FOSSIL_AI_MODEL");
  zTags = fossil_getenv("FOSSIL_AI_TAGS");

  if( zPrompt==0 && zRationale==0 && zReasoning==0 && zModel==0 && zTags==0 ){
    if( zComment==0 || zComment[0]==0 ){
      return;
    }
    zRationale = zComment;
    zTags = "commit";
  }else if( zRationale==0 && zComment!=0 && zComment[0]!=0 ){
    zRationale = zComment;
  }

  ai_schema_ensure();
  if( zReasoning && zReasoning[0] ){
    Blob reasoning = BLOB_INITIALIZER;
    char *zTitle = mprintf("Commit reasoning for rid %d", rid);
    blob_init(&reasoning, zReasoning, -1);
    reasoningNoteId = ai_note_create(
      0, zTitle, &reasoning, "reasoning", rid,
      "env:FOSSIL_AI_REASONING", "raw",
      "{\"origin\":\"commit-env\",\"field\":\"FOSSIL_AI_REASONING\"}"
    );
    blob_reset(&reasoning);
    fossil_free(zTitle);
  }

  db_prepare(&q,
    "INSERT INTO repository.ai_context("
    "  rid,prompt,rationale,reasoning_note_id,model_id,token_in,token_out,"
    "  created_at,tags"
    ") VALUES("
    "  :rid,:prompt,:rationale,:reasoning_note_id,:model_id,NULL,NULL,"
    "  julianday('now'),:tags"
    ");"
  );
  db_bind_int(&q, ":rid", rid);
  if( zPrompt ) db_bind_text(&q, ":prompt", zPrompt);
  else db_bind_null(&q, ":prompt");
  if( zRationale ) db_bind_text(&q, ":rationale", zRationale);
  else db_bind_null(&q, ":rationale");
  if( reasoningNoteId>0 ) db_bind_int(&q, ":reasoning_note_id", reasoningNoteId);
  else db_bind_null(&q, ":reasoning_note_id");
  if( zModel ) db_bind_text(&q, ":model_id", zModel);
  else db_bind_null(&q, ":model_id");
  if( zTags ) db_bind_text(&q, ":tags", zTags);
  else db_bind_null(&q, ":tags");
  db_step(&q);
  db_finalize(&q);
}

/*
** COMMAND: ai
**
** Usage: %fossil ai SUBCOMMAND ?OPTIONS?
**
** Subcommands:
**   enable     Enable AI features for this repository
**   disable    Disable AI features for this repository
**   init       Create AI tables if missing (implies enable)
**   status     Show AI enable flag and table presence
**   selftest   Insert and review test rows; use --keep to preserve them
*/
void ai_cmd(void){
  const char *zSub = g.argc>2 ? g.argv[2] : "status";
  int bEnabled;

  find_repository_option();
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);

  if( !g.repositoryOpen ){
    fossil_fatal("no repository is open");
  }

  if( fossil_strcmp(zSub, "enable")==0 ){
    verify_all_options();
    db_set_int("ai-enable", 1, 0);
    ai_schema_ensure();
    fossil_print("ai: enabled\n");
    return;
  }
  if( fossil_strcmp(zSub, "disable")==0 ){
    verify_all_options();
    db_set_int("ai-enable", 0, 0);
    fossil_print("ai: disabled\n");
    return;
  }
  if( fossil_strcmp(zSub, "init")==0 ){
    verify_all_options();
    db_set_int("ai-enable", 1, 0);
    ai_schema_ensure();
    fossil_print("ai: initialized\n");
    return;
  }
  if( fossil_strcmp(zSub, "status")==0 ){
    verify_all_options();
    bEnabled = ai_is_enabled();
    fossil_print("ai: %s\n", bEnabled ? "enabled" : "disabled");
    fossil_print(
      "tables: context=%d note=%d link=%d retrieval=%d retrieval_note=%d"
      " review=%d vector=%d policy=%d\n",
      db_table_exists("repository", "ai_context"),
      db_table_exists("repository", "ai_note"),
      db_table_exists("repository", "ai_note_link"),
      db_table_exists("repository", "ai_retrieval"),
      db_table_exists("repository", "ai_retrieval_note"),
      db_table_exists("repository", "ai_review"),
      db_table_exists("repository", "ai_vector"),
      db_table_exists("repository", "ai_policy")
    );
    return;
  }
  if( fossil_strcmp(zSub, "selftest")==0 ){
    Blob noteA = BLOB_INITIALIZER;
    Blob noteB = BLOB_INITIALIZER;
    int bKeep = find_option("keep", 0, 0)!=0;
    int ctxCount, noteCount, retrievalCount, reviewCount;
    int duplicateCount, linkCount;
    int nidA, nidB, qid;
    verify_all_options();
    if( !ai_is_enabled() ){
      fossil_fatal("ai is disabled; run 'fossil ai enable' first");
    }
    ai_schema_ensure();
    db_multi_exec(
      "INSERT INTO repository.ai_context("
      "  prompt,rationale,model_id,token_in,token_out,created_at,tags"
      ") VALUES("
      "  'selftest','schema smoke test','none',0,0,"
      "  julianday('now'),'selftest'"
      ");"
    );
    blob_init(&noteA, "# Selftest Topic\n\nShared note body.\n", -1);
    nidA = ai_note_create(
      1, "selftest note a", &noteA, "manual", 0, "selftest/a",
      "grouped", "{\"selftest\":true,\"slot\":\"a\"}"
    );
    blob_init(&noteB, "# Selftest Topic\n\nShared note body.\n", -1);
    nidB = ai_note_create(
      2, "selftest note b", &noteB, "manual", 0, "selftest/b",
      "curated", "{\"selftest\":true,\"slot\":\"b\"}"
    );
    db_multi_exec(
      "UPDATE repository.ai_note"
      "   SET title='note', process_level=NULL, metadata=NULL,"
      "       updated_at=julianday('now')"
      " WHERE nid=%d;",
      nidB
    );
    qid = ai_retrieval_begin(0, "selftest");
    ai_note_record_retrieval(qid, nidA, 1, 0.0, ai_tier_weight_value(1));
    ai_note_record_retrieval(qid, nidB, 2, 0.05, ai_tier_weight_value(2));
    ai_retrieval_review(qid);
    ctxCount = db_int(0,
      "SELECT count(*) FROM repository.ai_context WHERE tags='selftest'"
    );
    noteCount = db_int(0,
      "SELECT count(*) FROM repository.ai_note"
      " WHERE source_ref IN ('selftest/a','selftest/b')"
    );
    retrievalCount = db_int(0,
      "SELECT count(*) FROM repository.ai_retrieval WHERE query_text='selftest'"
    );
    reviewCount = db_int(0,
      "SELECT count(*) FROM repository.ai_review WHERE qid=%d",
      qid
    );
    duplicateCount = db_int(0,
      "SELECT count(*) FROM repository.ai_note"
      " WHERE source_ref IN ('selftest/a','selftest/b')"
      "   AND duplicate_of IS NOT NULL"
    );
    linkCount = db_int(0,
      "SELECT count(*) FROM repository.ai_note_link"
      " WHERE (from_nid IN (%d,%d) OR to_nid IN (%d,%d))"
      "   AND link_type IN ('co_retrieved','duplicate')",
      nidA, nidB, nidA, nidB
    );
    if( !bKeep ){
      db_multi_exec(
        "DELETE FROM repository.ai_review WHERE qid=%d;"
        "DELETE FROM repository.ai_retrieval_note WHERE qid=%d;"
        "DELETE FROM repository.ai_retrieval WHERE qid=%d;"
        "DELETE FROM repository.ai_note_link"
        " WHERE from_nid IN (%d,%d) OR to_nid IN (%d,%d);"
        "DELETE FROM repository.ai_vector"
        " WHERE source_type='note' AND source_id IN (%d,%d);"
        "DELETE FROM repository.ai_note WHERE nid IN (%d,%d);"
        "DELETE FROM repository.ai_context WHERE tags='selftest';",
        qid, qid, qid, nidA, nidB, nidA, nidB,
        nidA, nidB, nidA, nidB
      );
    }
    blob_reset(&noteA);
    blob_reset(&noteB);
    fossil_print(
      "selftest: context=%d note=%d retrieval=%d review=%d"
      " duplicate=%d link=%d%s\n",
      ctxCount, noteCount, retrievalCount, reviewCount,
      duplicateCount, linkCount, bKeep ? " keep=1" : ""
    );
    return;
  }

  verify_all_options();
  fossil_fatal("unknown subcommand: %s", zSub);
}
