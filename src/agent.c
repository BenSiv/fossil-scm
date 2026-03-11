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

static int agent_generate_embedding(
  const char *zModel,
  const char *zText,
  Blob *pOut
);

/*
** SETTING: agent-ollama-command width=40 default=ollama
**
** Command used by /agent-chat to invoke the local Ollama CLI.
*/
/*
** SETTING: agent-ollama-model width=20 default=llama3.2
**
** Default model name used by /agent-chat when the request does not
** specify a model explicitly.
*/

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
** Perform a semantic search for zQuery and append relevant snippets to pOut.
*/
static void agent_semantic_search(const char *zQuery, int nLimit, Blob *pOut){
  const char *zModel = db_get("agent-ollama-model", "llama3.2");
  Blob vQuery = BLOB_INITIALIZER;
  Stmt q;

  if( agent_generate_embedding(zModel, zQuery, &vQuery)!=0 ){
    blob_reset(&vQuery);
    return;
  }

  db_prepare(&q,
    "SELECT n.title, n.body, vec_distance(v.vector, :vec) AS dist"
    "  FROM ai_vector v, ai_note n"
    " WHERE v.source_type='note' AND v.source_id=n.nid"
    " ORDER BY dist ASC LIMIT %d",
    nLimit
  );
  db_bind_blob(&q, ":vec", &vQuery);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTitle = db_column_text(&q, 0);
    const char *zBody = db_column_text(&q, 1);
    blob_appendf(pOut, "\n--- Relevant Note: %s ---\n%s\n", zTitle, zBody);
  }
  db_finalize(&q);
  blob_reset(&vQuery);
}

/*
** Assemble a context summary of the current repository state into pOut.
*/
static void agent_assemble_context(Blob *pOut, const char *zQuery){
  int vid;
  Stmt q;
  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
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

    if( zQuery && zQuery[0] ){
      agent_semantic_search(zQuery, 3, pOut);
    }

    blob_appendf(pOut, "--- END CONTEXT ---\n\n");
  }
}

/*
** Invoke the local Ollama CLI and store its reply in pReply.
**
** Returns 0 on success and non-zero on error.
*/
static void agent_strip_ansi(Blob *pText);
static void agent_strip_prefix_noise(Blob *pText);

static int agent_run_ollama(
  const char *zModel,
  const char *zPrompt,
  Blob *pReply,
  Blob *pErr
){
  Blob cmd = BLOB_INITIALIZER;
  FILE *in;
  FILE *out = 0;
  int fdIn = -1;
  int childPid = 0;
  int rc;
  const char *zOllamaCmd = db_get("agent-ollama-command", "ollama");

  blob_zero(pReply);
  blob_zero(pErr);
  blob_append_escaped_arg(&cmd, zOllamaCmd, 1);
  blob_append(&cmd, " run", 4);
  blob_append_escaped_arg(&cmd, zModel, 0);
  blob_append(&cmd, " 2>&1", 5);
  rc = popen2(blob_str(&cmd), &fdIn, &out, &childPid, 0);
  if( rc!=0 || fdIn<0 || out==0 ){
    blob_appendf(pErr, "unable to run %s", zOllamaCmd);
    blob_reset(&cmd);
    return 1;
  }
  /* Send the prompt via stdin and close it so the child doesn't wait. */
  fprintf(out, "%s", zPrompt);
  fclose(out);
  out = 0;
  in = fdopen(fdIn, "rb");
  if( in==0 ){
    pclose2(fdIn, out, childPid);
    blob_appendf(pErr, "unable to read output from %s", zOllamaCmd);
    blob_reset(&cmd);
    return 1;
  }
  blob_read_from_channel(pReply, in, -1);
  pclose2(fdIn, out, childPid);
  agent_strip_ansi(pReply);
  agent_strip_prefix_noise(pReply);
  blob_trim(pReply);
  if( blob_size(pReply)==0 ){
    blob_appendf(pErr,
      "ollama command failed for model \"%s\"", zModel
    );
    blob_reset(&cmd);
    return 1;
  }
  if( fossil_strncmp(blob_str(pReply), "Error:", 6)==0 ){
    blob_append(pErr, blob_str(pReply), blob_size(pReply));
    blob_reset(&cmd);
    return 1;
  }
  blob_reset(&cmd);
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
** Generate an embedding for zText using the configured Ollama model.
** Returns 0 on success, non-zero on error.
** The result is stored as an array of floats in pOut.
*/
static int agent_generate_embedding(
  const char *zModel,
  const char *zText,
  Blob *pOut
){
  Blob cmd = BLOB_INITIALIZER;
  Blob reply = BLOB_INITIALIZER;
  char *z;
  FILE *p;

  blob_appendf(&cmd, "curl -s -X POST http://localhost:11434/api/embeddings "
                     "-H \"Content-Type: application/json\" -d ");
  {
    Blob json = BLOB_INITIALIZER;
    blob_appendf(&json, "{\"model\":%!j, \"prompt\":%!j}", zModel, zText);
    blob_append_sql(&cmd, "%$", blob_str(&json));
    blob_reset(&json);
  }
  /* fossil_print("DEBUG cmd: %s\n", blob_str(&cmd)); */
  p = popen(blob_str(&cmd), "r");
  if( p==0 ){
    blob_reset(&cmd);
    return 1;
  }
  blob_read_from_channel(&reply, p, -1);
  /* fossil_print("DEBUG reply: %s\n", blob_str(&reply)); */
  pclose(p);
  blob_reset(&cmd);

  /* Minimalist JSON parsing for "embedding":[...] */
  z = strstr(blob_str(&reply), "\"embedding\":[");
  if( z==0 ){
    fossil_print("DEBUG: Embedding key not found in response: %s\n", blob_str(&reply));
    blob_reset(&reply);
    return 1;
  }
  z += 13;
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
  const char *zModel = db_get("agent-ollama-model", "llama3.2");
  Stmt q1, q2;
  int n = 0;

  db_prepare(&q1,
    "SELECT nid, title, body FROM ai_note"
    " WHERE nid NOT IN (SELECT source_id FROM ai_vector WHERE source_type='note')"
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
  Blob body = BLOB_INITIALIZER;
  int tier = 1;

  zTitle = find_option("title", 0, 1);
  if( find_option("tier-2", 0, 0) ) tier = 2;
  verify_all_options();
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
    usage("note ?TEXT|FILE? [--title TEXT] [--tier-2]");
  }
  if( zTitle==0 ) zTitle = "Note";

  ai_schema_ensure();
  db_multi_exec(
    "INSERT INTO ai_note(tier, title, body, source_type, created_at)"
    " VALUES(%d, %Q, %B, 'manual', julianday('now'))",
    tier, zTitle, &body
  );
  fossil_print("Added note: %s\n", zTitle);
  blob_reset(&body);
}

/*
** COMMAND: agent
**
** Usage: %fossil agent SUBCOMMAND ...
**
** Commands intended to help agent-style development workflows while keeping
** the integration within Fossil's existing command and wiki model.
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
**    fossil agent note ?FILE? [--title TEXT] [--tier-2]
**       Add a new note to the AI knowledge base.
**
**    fossil agent wiki-sync PAGENAME ?FILE? [--append] [--dry-run]
**                             [--title TEXT] [--status TEXT]
**       Create or update PAGENAME with an agent-authored manager update.
**       The body comes from FILE or stdin.  Checkout metadata and current
**       pending changes are added as context for the human reader.
*/
void agent_cmd(void){
  const char *zCmd;
  const char *zModel = db_get("agent-ollama-model", "llama3.2");

  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  if( g.argc<3 ){
    usage("SUBCOMMAND ...");
  }
  zCmd = g.argv[2];
  if( fossil_strcmp(zCmd, "repomap")==0 ){
    agent_repomap_cmd();
  }else if( fossil_strcmp(zCmd, "changes")==0 ){
    agent_changes_cmd();
  }else if( fossil_strcmp(zCmd, "note")==0 ){
    agent_note_cmd();
  }else if( fossil_strcmp(zCmd, "embed")==0 ){
    Blob v = BLOB_INITIALIZER;
    if( g.argc<4 ) usage("agent embed TEXT");
    if( agent_generate_embedding(zModel, g.argv[3], &v)==0 ){
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
  const char *zModel = db_get("agent-ollama-model", "llama3.2");

  login_check_credentials();
  if( !g.perm.Read ){
    login_needed(g.anon.Read);
    return;
  }
  style_set_current_feature("agent");
  style_header("Agent Chat");
  @ <div class="fossil-doc" data-title="Agent Chat">
  @ <p>This page sends prompts to a local Ollama CLI command configured by
  @ the repository settings.</p>
  @ <div class="forumEdit">
  @ <label for="agent-model"><b>Model:</b></label>
  @ <input type="text" id="agent-model" size="24" value="%h(zModel)">
  @ &nbsp;&nbsp;
  @ <label><input type="checkbox" id="agent-context" checked> <b>Context Awareness</b></label>
  @ </div>
  @ <div id="agent-chat-log"
  @  style="min-height:320px;max-height:520px;overflow:auto;border:1px solid
  @  #888;padding:0.8em;margin:1em 0;background:rgba(127,127,127,0.05);">
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
  @   var input = document.getElementById('agent-chat-input');
  @   var send = document.getElementById('agent-chat-send');
  @   var model = document.getElementById('agent-model');
  @   var context = document.getElementById('agent-context');
  @   var log = document.getElementById('agent-chat-log');
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
  @   send.addEventListener('click', function(){
  @     var msg = input.value.trim();
  @     if(!msg) return;
  @     addMsg('You', msg);
  @     input.value = '';
  @     fetch('agent-chat', {
  @       method: 'POST',
  @       headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
  @       body: new URLSearchParams({msg: msg, model: model.value, context: context.checked ? 1 : 0})
  @     }).then(function(r){
  @       return r.json();
  @     }).then(function(data){
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
  style_finish_page();
}

/*
** WEBPAGE: agent-chat
**
** JSON endpoint for the local Ollama-backed agent chat UI.
*/
void agent_chat_page(void){
  Blob reply = BLOB_INITIALIZER;
  Blob err = BLOB_INITIALIZER;
  const char *zMsg;
  const char *zModel;
  int rc;
  char *zContextMsg = 0;

  login_check_credentials();
  zMsg = PD("msg", "");
  zModel = PD("model", db_get("agent-ollama-model", "llama3.2"));
  cgi_set_content_type("application/json");
  if( zMsg[0]==0 ){
    CX("{\"error\":%!j}\n", "missing msg parameter");
    return;
  }
  if( PB("context") ){
    Blob ctx = BLOB_INITIALIZER;
    agent_assemble_context(&ctx, zMsg);
    blob_appendf(&ctx, "%s", zMsg);
    zContextMsg = fossil_strdup(blob_str(&ctx));
    zMsg = zContextMsg;
    blob_reset(&ctx);
  }
  if( zModel[0]==0 ){
    CX("{\"error\":%!j}\n", "missing model parameter");
    return;
  }
  rc = agent_run_ollama(zModel, zMsg, &reply, &err);
  if( rc==0 ){
    CX("{\"model\":%!j,\"reply\":%!j}\n", zModel, blob_str(&reply));
  }else{
    const char *zErr = blob_size(&err)>0 ? blob_str(&err)
                                         : "ollama invocation failed";
    CX("{\"model\":%!j,\"error\":%!j}\n", zModel, zErr);
  }
  if( PB("context") ) fossil_free((char*)zMsg);
  blob_reset(&reply);
  blob_reset(&err);
}
