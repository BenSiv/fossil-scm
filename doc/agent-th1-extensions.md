# Fossil AI TH1 Extensions

This document outlines the TH1 extension mechanisms available for Fossil's native AI agent feature.

Fossil's AI backend handles tool execution and context gathering natively. However, to allow repository administrators to define custom features specific to their workflows—without requiring modifications to Fossil's C source code—Fossil provides a robust TH1 plugin architecture, inspired by [Project Pi](https://github.com/mariozechner/pi-mono).

## Dynamic Capabilities (Custom Tools)

System administrators can create new tools that the agent can invoke to interact with the repository or external systems.

To register a dynamic tool, add a TH1 script to your repository's setup configuration (e.g., in a `th1-agent-setup` parameter or block). Use the `agent_capability_register` command:

```tcl
agent_capability_register \
  "query_jira_ticket" \
  "Fetches the status and summary of a Jira ticket from the external issue tracker." \
  "{\"type\":\"object\",\"properties\":{\"ticket_id\":{\"type\":\"string\"}},\"required\":[\"ticket_id\"]}" \
  0 1 1 \
  {
     # This TH1 script is evaluated when the tool is called.
     # Arguments requested by the AI are provided in the $agent_tool_args variable
     # as a raw JSON string.

     set ticket_id [agent_json_extract $agent_tool_args "$.ticket_id"]
     set result [http "https://jira.example.com/rest/api/2/issue/$ticket_id"]
     return "JIRA Ticket Info: $result"
  }
```

### Signature
`agent_capability_register NAME DESC SCHEMA WRITE_REQ NET_REQ CONFIRM_REQ SCRIPT`

- `NAME`: The function name exposed to the LLM.
- `DESC`: The description explaining when the AI should use it.
- `SCHEMA`: The JSON schema defining expected arguments.
- `WRITE_REQ`: (1 or 0) Does this tool require database write access?
- `NET_REQ`: (1 or 0) Does this tool require network access?
- `CONFIRM_REQ`: (1 or 0) If 1, the AI backend strictly requires user approval in the web UI. If 0, the tool can run autonomously.
- `SCRIPT`: The actual TH1 script to evaluate. The TH1 engine exposes `$agent_tool_args` (a string containing the JSON payload from the AI) and expects the script's return string to be the output.

## Interceptors (Tool Middleware)

Sometimes you want to sanitize, modify, or block tool requests transparently, much like `beforeToolCall` or `afterToolCall` middleware in standard web development.

Fossil lets you define two special TH1 procedures: `th1-agent-pre-tool` and `th1-agent-post-tool`. If these exist in the TH1 context when a tool is called, they are executed.

### Pre-Tool Interceptor

The pre-tool interceptor runs *before* every tool (including built-ins). It has access to `$agent_tool_name` and `$agent_tool_args`.

```tcl
proc th1-agent-pre-tool {} {
  global agent_tool_name
  global agent_tool_args
  
  if {$agent_tool_name eq "edit_file"} {
    set path [agent_json_extract $agent_tool_args "$.path"]
    if {[string match "*.restricted" $path]} {
      # Throwing an error aborts execution
      error "Permission denied: AI is not allowed to edit restricted files."
    }
  }
}
```

### Post-Tool Interceptor

The post-tool interceptor runs *after* execution. It gives you a chance to summarize or alter the tool's result before it is inserted into the chat stream. It has access to `$agent_tool_result`.

```tcl
proc th1-agent-post-tool {} {
  global agent_tool_result
  
  # For example, truncating massively large outputs to preserve context window
  set len [string length $agent_tool_result]
  if {$len > 2000} {
    set trunk [string range $agent_tool_result 0 2000]
    return "$trunk\n\n[TRUNCATED: Output exceeded 2000 chars]"
  }
  return $agent_tool_result
}
```

Make sure the `th1-agent-post-tool` procedure returns a string, as its return value completely replaces the original tool output.
