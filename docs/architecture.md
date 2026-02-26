# Architecture

## Overview

art is a single-threaded C11 CLI application that sends prompts to LLM APIs and
executes tool calls on behalf of the model. It communicates with any
OpenAI-compatible chat completions endpoint using HTTP streaming (Server-Sent
Events).

## Module Dependency Graph

```
main.c
├── config     (YAML configuration loading)
├── prompts    (named prompt file management)
├── http       (libcurl HTTP streaming client)
│   └── sse    (SSE line parser)
├── agent      (conversation state, message history)
│   └── api    (request building, delta parsing)
├── runner     (tool approval + agent loop)
│   └── tools  (tool registry + executors)
├── session    (session persistence to markdown)
└── buf        (dynamic string buffer, used everywhere)
```

## Layers

### CLI Layer (`main.c`)

Parses command-line arguments with `getopt_long`, resolves `@file` attachments,
and handles administrative commands (`--install`, `--list-agents`,
`--list-prompts`, `--add-prompt`, `--new-prompt`). Constructs the user message
from positional arguments, file attachments, and stdin. Orchestrates
initialization and cleanup of all subsystems.

### Configuration Layer (`config.c`)

Loads YAML configuration from two locations:

1. `~/.artifice/config.yaml` (global)
2. `./.artifice/config.yaml` (local, overrides global)

Parses agent definitions (model, API key, provider, base URL, system prompt,
tool list) and global settings (default agent, tool approval mode, session
saving, tool allowlist).

### Agent Layer (`agent.c`)

Maintains the conversation as a cJSON array of messages in OpenAI chat format.
Handles message lifecycle: adding user messages, recording assistant responses
with tool calls, and inserting tool results. Drives the streaming API request
through `http` and `api` modules, accumulating text chunks and tool call
fragments from SSE deltas.

### Runner Layer (`runner.c`)

Implements the agentic loop: send prompt, check for tool calls, get approval,
execute tools, feed results back, repeat. Caps at 50 tool turns per invocation.
Contains the approver subsystem that implements three modes (`ask`, `auto`,
`deny`) with an interactive prompt and session-scoped "always allow" memory.

### Tool Layer (`tools.c`)

Defines five built-in tools (read, write, glob, edit, shell) as a static
registry of function pointers. Each tool has an OpenAI-compatible JSON schema
built at startup and an executor function that takes cJSON arguments and returns
a malloc'd result string. Tools are selected by fnmatch patterns.

### Network Layer (`http.c`, `sse.c`, `api.c`)

- **http.c**: Wraps libcurl for HTTPS POST with SSE streaming. Auto-detects CA
  bundle paths across distributions. Sends to `{base_url}/chat/completions`.
- **sse.c**: Byte-by-byte line buffer that extracts `data: ` payloads from the
  SSE stream, skipping the `[DONE]` sentinel.
- **api.c**: Builds the chat completions JSON request body and parses individual
  SSE delta chunks into content fragments, tool call fragments, and token usage.

### Session Layer (`session.c`)

Saves completed conversations to
`~/.artifice/sessions/YYYY-MM-DD-HHMMSS-uuuuuu.md` as markdown with metadata
headers (timestamp, model, provider, system prompt).

### Buffer Utility (`buf.c`)

Dynamic byte buffer with exponential growth (doubling from 256 bytes). Provides
append, printf, clear, and detach operations. Used throughout the codebase for
string assembly.

## Data Flow

```
User Input                   API Response (SSE stream)
    │                              │
    ▼                              ▼
build_user_message()         sse_feed()
    │                              │
    ▼                              ▼
agent_add_user_message()     api_parse_delta()
    │                              │
    ▼                              ▼
agent_send()  ──────────►    on_sse_event()
    │                         │         │
    │                    text chunk   tool call
    │                         │      fragment
    │                         ▼         │
    │                    on_chunk()      │
    │                    (print to       ▼
    │                     stdout)   accumulate in
    │                              agent_response_t
    ▼
run_agent_loop()
    │
    ├── approver_check() ──► interactive prompt
    │
    ├── tools_execute()  ──► tool_read/write/glob/edit/shell
    │
    ├── agent_add_tool_result()
    │
    └── agent_send("") ─────► (loop back for follow-up)
```

## Key Design Decisions

**Single binary, minimal dependencies.** The only external libraries are
libcurl (HTTP), libyaml (config), and the vendored cJSON (JSON). No runtime
dependencies beyond POSIX.

**Streaming-first.** Text is printed to stdout as it arrives via SSE chunks,
giving immediate feedback. Tool call fragments are accumulated separately and
only processed once the stream completes.

**OpenAI-compatible protocol.** The API layer targets the `/chat/completions`
endpoint with standard message roles and tool schemas. This makes art compatible
with any provider that implements this protocol (OpenAI, Anthropic via proxy,
local inference servers like ollama, vLLM, etc.).

**Interactive approval by default.** Tool execution requires explicit user
consent unless configured otherwise, giving the user control over what the model
can do on their system.

**Explicit resource management.** Every module follows an `init`/`free` pattern.
Memory is tracked through ownership conventions and freed on all error paths.
