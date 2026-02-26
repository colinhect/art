# API Protocol

art communicates with LLM providers using the OpenAI chat completions API over
HTTP with Server-Sent Events (SSE) streaming.

## Endpoint

```
POST {base_url}/chat/completions
```

The base URL defaults to `https://api.openai.com/v1` and can be overridden per
agent in the config file.

## Request Headers

```
Content-Type: application/json
Accept: text/event-stream
Authorization: Bearer {api_key}
```

## Request Body

```json
{
  "model": "gpt-4o-mini",
  "stream": true,
  "stream_options": {
    "include_usage": true
  },
  "messages": [
    {"role": "system", "content": "..."},
    {"role": "user", "content": "..."},
    {"role": "assistant", "content": "...", "tool_calls": [...]},
    {"role": "tool", "tool_call_id": "...", "content": "..."}
  ],
  "tools": [
    {
      "type": "function",
      "function": {
        "name": "read",
        "description": "Read the contents of a file.",
        "parameters": {
          "type": "object",
          "required": ["path"],
          "properties": {
            "path": {"type": "string", "description": "..."},
            "offset": {"type": "integer", "description": "..."},
            "limit": {"type": "integer", "description": "..."}
          }
        }
      }
    }
  ],
  "tool_choice": "auto"
}
```

The `tools` array is only included when the agent has tools enabled. When
present, `tool_choice` is set to `"auto"`, letting the model decide when to call
tools.

## Response (SSE Stream)

The server sends a stream of `data:` lines, each containing a JSON chunk:

```
data: {"id":"...","choices":[{"delta":{"content":"Hello"}}]}
data: {"id":"...","choices":[{"delta":{"content":" world"}}]}
data: {"id":"...","choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_abc","function":{"name":"read","arguments":""}}]}}]}
data: {"id":"...","choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"path"}}]}}]}
data: {"id":"...","choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"\":\"/tmp/f\"}"}}]}}]}
data: {"id":"...","usage":{"prompt_tokens":100,"completion_tokens":50}}
data: [DONE]
```

### Delta Parsing

Each SSE data line is parsed into a `delta_t` structure:

| Field              | Description                                          |
|--------------------|------------------------------------------------------|
| `content`          | Text fragment (may be NULL).                         |
| `tool_call_index`  | Index of the tool call (-1 if none in this delta).   |
| `tc_id`            | Tool call ID (first fragment only).                  |
| `tc_name`          | Function name (first fragment only).                 |
| `tc_arguments`     | JSON arguments fragment (accumulated across deltas). |
| `input_tokens`     | Prompt token count (from usage, typically last chunk).|
| `output_tokens`    | Completion token count.                              |

Tool call arguments arrive split across multiple SSE events. The agent
accumulates argument fragments by tool call index and parses the complete JSON
once the stream ends.

## Message Roles

| Role        | Purpose                                                    |
|-------------|------------------------------------------------------------|
| `system`    | System prompt. Added as the first message if set.          |
| `user`      | User input (prompt + file attachments + stdin).            |
| `assistant` | Model response. May include `tool_calls` array.           |
| `tool`      | Tool execution result. Includes `tool_call_id` reference.  |

## Multi-Turn Tool Calling

When the model returns tool calls, the conversation proceeds:

1. Assistant message with `tool_calls` is added to history.
2. Each tool is executed (or denied).
3. A `tool` role message with the result is added for each tool call.
4. An empty user prompt triggers the next model response.
5. Repeat until the model responds without tool calls or the 50-turn limit is
   reached.

## SSL/TLS

The HTTP client auto-detects CA certificate bundle paths for common
distributions:

- `/etc/ssl/certs/ca-certificates.crt` (Debian/Ubuntu)
- `/etc/pki/tls/certs/ca-bundle.crt` (RHEL/Fedora)
- `/etc/ssl/cert.pem` (macOS, Alpine)
- `/etc/ca-certificates/extracted/tls-ca-bundle.pem` (Arch)
