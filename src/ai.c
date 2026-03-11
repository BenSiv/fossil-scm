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

/* Record AI provenance for a commit if environment context is present. */
void ai_record_commit(int rid, const char *zComment);

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

  if( sqlite3_value_type(argv[0])!=SQLITE_BLOB
   || sqlite3_value_type(argv[1])!=SQLITE_BLOB
  ){
    sqlite3_result_null(context);
    return;
  }

  a = (const float*)sqlite3_value_blob(argv[0]);
  nA = sqlite3_value_bytes(argv[0]) / sizeof(float);
  b = (const float*)sqlite3_value_blob(argv[1]);
  nB = sqlite3_value_bytes(argv[1]) / sizeof(float);

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
** Register AI-related SQL functions with the database connection.
*/
void ai_add_sql_func(sqlite3 *db){
  sqlite3_create_function(db, "vec_distance", 2,
                          SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                          0, ai_vec_distance_sqlfunc, 0, 0);
}

int ai_is_enabled(void){
  return db_get_boolean("ai-enable", 0);
}

void ai_schema_ensure(void){
  if( !ai_is_enabled() ) return;

  db_multi_exec(
    "CREATE TABLE IF NOT EXISTS repository.ai_context("
    "  cid INTEGER PRIMARY KEY,"
    "  rid INTEGER REFERENCES blob,"
    "  prompt TEXT,"
    "  rationale TEXT,"
    "  model_id TEXT,"
    "  token_in INTEGER,"
    "  token_out INTEGER,"
    "  created_at DATETIME DEFAULT (julianday('now')) ,"
    "  tags TEXT"
    ");"
    "CREATE TABLE IF NOT EXISTS repository.ai_note("
    "  nid INTEGER PRIMARY KEY,"
    "  tier INTEGER,"
    "  title TEXT,"
    "  body TEXT,"
    "  source_type TEXT,"
    "  source_id INTEGER,"
    "  heat REAL DEFAULT 1.0,"
    "  created_at DATETIME DEFAULT (julianday('now')) ,"
    "  updated_at DATETIME"
    ");"
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

  db_multi_exec(
    "CREATE INDEX IF NOT EXISTS repository.ai_note_i1 ON ai_note(tier, heat);"
    "CREATE INDEX IF NOT EXISTS repository.ai_context_i1 ON ai_context(rid);"
    "CREATE INDEX IF NOT EXISTS repository.ai_vector_i1 ON ai_vector(source_type, source_id);"
  );
}

void ai_record_commit(int rid, const char *zComment){
  const char *zPrompt = 0;
  const char *zRationale = 0;
  const char *zModel = 0;
  const char *zTags = 0;

  if( !ai_is_enabled() ) return;

  zPrompt = fossil_getenv("FOSSIL_AI_PROMPT");
  zRationale = fossil_getenv("FOSSIL_AI_RATIONALE");
  zModel = fossil_getenv("FOSSIL_AI_MODEL");
  zTags = fossil_getenv("FOSSIL_AI_TAGS");

  if( zPrompt==0 && zRationale==0 && zModel==0 && zTags==0 ){
    if( zComment==0 || zComment[0]==0 ){
      /* No AI context and no commit comment. */
      return;
    }
    zRationale = zComment;
    zTags = "commit";
  }else if( zRationale==0 && zComment!=0 && zComment[0]!=0 ){
    zRationale = zComment;
  }

  ai_schema_ensure();
  db_multi_exec(
    "INSERT INTO repository.ai_context(rid,prompt,rationale,model_id,token_in,token_out,tags)"
    " VALUES(%d,%Q,%Q,%Q,NULL,NULL,%Q);",
    rid, zPrompt, zRationale, zModel, zTags
  );
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
**   selftest   Insert and remove a test row to verify basic operation
*/
void ai_cmd(void){
  const char *zSub = g.argc>2 ? g.argv[2] : "status";

  find_repository_option();
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  verify_all_options();

  if( !g.repositoryOpen ){
    fossil_fatal("no repository is open");
  }

  if( fossil_strcmp(zSub, "enable")==0 ){
    db_set_int("ai-enable", 1, 0);
    ai_schema_ensure();
    fossil_print("ai: enabled\n");
    return;
  }
  if( fossil_strcmp(zSub, "disable")==0 ){
    db_set_int("ai-enable", 0, 0);
    fossil_print("ai: disabled\n");
    return;
  }
  if( fossil_strcmp(zSub, "init")==0 ){
    db_set_int("ai-enable", 1, 0);
    ai_schema_ensure();
    fossil_print("ai: initialized\n");
    return;
  }
  if( fossil_strcmp(zSub, "status")==0 ){
    int bEnabled = ai_is_enabled();
    int bCtx = db_table_exists("repository", "ai_context");
    int bNote = db_table_exists("repository", "ai_note");
    int bVec = db_table_exists("repository", "ai_vector");
    int bPol = db_table_exists("repository", "ai_policy");
    fossil_print("ai: %s\n", bEnabled ? "enabled" : "disabled");
    fossil_print("tables: context=%d note=%d vector=%d policy=%d\n",
                 bCtx, bNote, bVec, bPol);
    return;
  }
  if( fossil_strcmp(zSub, "selftest")==0 ){
    int nCtx;
    if( !ai_is_enabled() ){
      fossil_fatal("ai is disabled; run 'fossil ai enable' first");
    }
    ai_schema_ensure();
    db_multi_exec(
      "INSERT INTO repository.ai_context(prompt, rationale, model_id, token_in, token_out, tags)"
      " VALUES('selftest','schema smoke test','none',0,0,'selftest');"
    );
    nCtx = db_int(0, "SELECT count(*) FROM repository.ai_context WHERE tags='selftest'");
    db_multi_exec("DELETE FROM repository.ai_context WHERE tags='selftest';");
    fossil_print("selftest: ai_context rows inserted=%d\n", nCtx);
    return;
  }

  fossil_fatal("unknown subcommand: %s", zSub);
}
