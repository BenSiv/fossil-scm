/*
** Copyright (c) 2026
**
** Backend runtime and streaming helpers for the Fossil agent surfaces.
*/
#include "config.h"
#include "agent.h"
#include "agent_internal.h"

void blob_append_escaped_arg(Blob *pBlob, const char *zIn, int isFilename);
void blob_resize(Blob *pBlob, unsigned int newSize);
int fossil_strnicmp(const char *zA, const char *zB, int nByte);

static const AgentToolDef aAgentTools[] = {
  {"chat-backend", "Invoke the configured chat backend", "backend", 0, 1},
  {"list_files", "List repository files from the current checkout", "repo", 0, 1},
  {"read_file", "Read a file from the working tree", "file", 0, 1},
  {"edit_file", "Apply a proposed file edit", "file", 1, 1},
  {"semantic_search", "Search indexed repository knowledge", "knowledge", 0, 1},
  {0, 0, 0, 0, 0}
};

const AgentToolDef *agent_tool_find(const char *zName){
  int i;
  if( zName==0 || zName[0]==0 ) return 0;
  for(i=0; aAgentTools[i].zName; i++){
    if( fossil_strcmp(aAgentTools[i].zName, zName)==0 ){
      return &aAgentTools[i];
    }
  }
  return 0;
}

void agent_emit_tool_json(const char *zName){
  const AgentToolDef *pTool = agent_tool_find(zName);
  if( pTool==0 ){
    CX("null");
    return;
  }
  CX("{\"name\":%!j,\"description\":%!j,\"kind\":%!j,"
     "\"requires_confirmation\":%s,\"builtin\":%s}",
     pTool->zName, pTool->zDescription, pTool->zKind,
     pTool->bRequiresConfirm ? "true" : "false",
     pTool->bBuiltin ? "true" : "false");
}

void agent_emit_tool_array_json(void){
  int i;
  CX("[");
  for(i=0; aAgentTools[i].zName; i++){
    if( i>0 ) CX(",");
    agent_emit_tool_json(aAgentTools[i].zName);
  }
  CX("]");
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
** Expand zTemplate into pOut. Replaces %m with the shell-escaped model name
** and %% with a literal percent sign.
*/
void agent_expand_command(
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
** Wrap the command in a stable shell invocation with exported agent env vars.
*/
void agent_prepare_command(
  Blob *pOut,
  const char *zMode,
  const AgentSessionContext *pCtx,
  Blob *pCmd
){
  Blob model = BLOB_INITIALIZER;
  Blob mode = BLOB_INITIALIZER;
  Blob provider = BLOB_INITIALIZER;
  Blob interface = BLOB_INITIALIZER;
  Blob cmd = BLOB_INITIALIZER;
  blob_append_escaped_arg(&model, pCtx->zModel ? pCtx->zModel : "", 0);
  blob_append_escaped_arg(&mode, zMode ? zMode : "", 0);
  blob_append_escaped_arg(&provider, pCtx->zProvider ? pCtx->zProvider : "", 0);
  blob_append_escaped_arg(&interface, 
    pCtx->interface == PROV_IFACE_JSON_STDIO ? "json-stdio" : "prompt", 0);
  blob_append_escaped_arg(&cmd, blob_str(pCmd), 0);
  blob_zero(pOut);
  blob_appendf(
    pOut,
    "env FOSSIL_AGENT_MODEL=%s FOSSIL_AGENT_MODE=%s FOSSIL_AGENT_PROVIDER=%s "
    "FOSSIL_AGENT_INTERFACE=%s sh -lc %s 2>&1",
    blob_str(&model), blob_str(&mode), blob_str(&provider), 
    blob_str(&interface), blob_str(&cmd)
  );
  blob_reset(&model);
  blob_reset(&mode);
  blob_reset(&provider);
  blob_reset(&interface);
  blob_reset(&cmd);
}

/*
** Validate a provider/model pair before invoking the backend.
*/
int agent_validate_provider_model(
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
** Remove ANSI/VT100 escape sequences from CLI output so the web UI gets
** readable text instead of terminal control codes.
*/
void agent_strip_ansi(Blob *pText){
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
void agent_strip_prefix_noise(Blob *pText){
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
** Serialize the session context into a standardized JSON object for
** consumption by the provider bridge.
*/
void agent_serialize_context_json(Blob *pOut, const AgentSessionContext *pCtx){
  int i;
  blob_appendf(pOut, "{\"sid\":%d,\"provider\":%!j,\"model\":%!j,",
    pCtx->sid, pCtx->zProvider, pCtx->zModel);
  blob_appendf(pOut, "\"system_prompt\":%!j,\"messages\":[",
    pCtx->zSystemPrompt ? pCtx->zSystemPrompt : "");
  for(i=0; i<pCtx->nMsg; i++){
    blob_appendf(pOut, "%s{\"role\":%!j,\"content\":%!j}",
      i ? "," : "", pCtx->aMsg[i].zRole, pCtx->aMsg[i].zContent);
  }
  blob_appendf(pOut, "],\"query\":%!j}", pCtx->zQuery ? pCtx->zQuery : "");
}

/*
** SSE chunk handler: emits text as a "data:" SSE event.
*/
void agent_sse_handler(const char *zChunk, int nChunk, void *pApp){
  if( nChunk<=0 ) return;
  if( zChunk && nChunk>0 ){
    Blob tmp = BLOB_INITIALIZER;
    blob_append(&tmp, zChunk, nChunk);
    agent_strip_ansi(&tmp);
    if( blob_size(&tmp)>0 ){
      const char *z = blob_str(&tmp);
      const char *zOrig = z;
      while( fossil_isspace(z[0]) ) z++;
      if( z[0]=='{' ){
        /* This is likely a structured event from a modern bridge.
        ** Emit it literally (assumed to be a single message object). */
        CX("data: %s\n\n", z);
      }else{
        /* Legacy raw text chunk. Shield it as a JSON string literal. */
        CX("data: %!j\n\n", zOrig);
      }
      fflush(stdout);
    }
    blob_reset(&tmp);
  }
  (void)pApp;
}

/*
** Invoke the configured agent backend and store its reply in pReply.
**
** Returns 0 on success and non-zero on error.
*/
int agent_run_backend_core(
  const AgentSessionContext *pCtx,
  Blob *pReply,
  Blob *pErr,
  void (*xChunk)(const char*, int, void*),
  void *pApp
){
  Blob cmd = BLOB_INITIALIZER;
  Blob envCmd = BLOB_INITIALIZER;
  Blob prompt = BLOB_INITIALIZER;
  Blob err = BLOB_INITIALIZER;
  Blob *pErrUse = pErr ? pErr : &err;
  FILE *in;
  FILE *out = 0;
  int fdIn = -1;
  int childPid = 0;
  int rc;
  const char *zCmdTmpl = agent_command_template();

  if( pReply ) blob_zero(pReply);
  blob_zero(pErrUse);
  if( agent_validate_provider_model(pCtx->zProvider, pCtx->zModel, pErrUse) ){
    if( pErr==0 ) blob_reset(&err);
    return 1;
  }
  agent_expand_command(&cmd, zCmdTmpl, pCtx->zModel);
  agent_prepare_command(&envCmd, "chat", pCtx, &cmd);
  
  if( pCtx->interface == PROV_IFACE_JSON_STDIO ){
    agent_serialize_context_json(&prompt, pCtx);
  }else{
    blob_append(&prompt, pCtx->zQuery ? pCtx->zQuery : "", -1);
  }

  rc = popen2(blob_str(&envCmd), &fdIn, &out, &childPid, 0);
  if( rc!=0 || fdIn<0 || out==0 ){
    blob_appendf(pErrUse, "unable to run configured agent command");
    blob_reset(&cmd);
    blob_reset(&envCmd);
    blob_reset(&prompt);
    if( pErr==0 ) blob_reset(&err);
    return 1;
  }
  fprintf(out, "%s", blob_str(&prompt));
  fclose(out);
  out = 0;
  in = fdopen(fdIn, "rb");
  if( in==0 ){
    pclose2(fdIn, out, childPid);
    blob_appendf(pErrUse, "unable to read output from configured agent command");
    blob_reset(&cmd);
    blob_reset(&envCmd);
    blob_reset(&prompt);
    if( pErr==0 ) blob_reset(&err);
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
    /* For JSON interface, we might eventually want to parse but for now
    ** we treat the output as semi-structured text or raw response. */
    agent_strip_ansi(pReply);
    agent_strip_prefix_noise(pReply);
    blob_trim(pReply);
    if( blob_size(pReply)==0 ){
      blob_appendf(pErrUse, "agent backend returned an empty reply");
      blob_reset(&cmd);
      blob_reset(&envCmd);
      blob_reset(&prompt);
      if( pErr==0 ) blob_reset(&err);
      return 1;
    }
  }
  blob_reset(&cmd);
  blob_reset(&envCmd);
  blob_reset(&prompt);
  if( pErr==0 ) blob_reset(&err);
  return 0;
}
