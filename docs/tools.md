# Built-in Tools

art provides five tools that the LLM can call during a conversation. Tools are
exposed to the model as OpenAI function-calling schemas and selected via fnmatch
patterns (e.g. `--tools '*'` enables all tools, `--tools 'read,glob'` enables
only read and glob).

## read

Read the contents of a file with line numbers.

**Parameters:**

| Name     | Type    | Required | Description                              |
|----------|---------|----------|------------------------------------------|
| `path`   | string  | yes      | Absolute or relative file path.          |
| `offset` | integer | no       | Line number to start from (0-based).     |
| `limit`  | integer | no       | Maximum number of lines to return.       |

**Behavior:**
- Resolves `~` to `$HOME` and relative paths to absolute via `realpath`.
- Returns line-numbered output in `NNNN | content` format.
- Rejects files larger than 1 MB.
- Returns `"(empty file)"` for zero-length files.

**Example output:**
```
   1 | #include <stdio.h>
   2 |
   3 | int main(void) {
   4 |     printf("hello\n");
   5 |     return 0;
   6 | }
```

## write

Create or overwrite a file.

**Parameters:**

| Name      | Type   | Required | Description                    |
|-----------|--------|----------|--------------------------------|
| `path`    | string | yes      | Absolute or relative file path.|
| `content` | string | yes      | Content to write.              |

**Behavior:**
- Creates parent directories automatically (`mkdir -p`).
- Returns a JSON object with `success`, `path`, `old_lines`, `new_lines`, and
  `is_new_file`.

**Example output:**
```json
{"success":true,"path":"src/hello.c","old_lines":0,"new_lines":6,"is_new_file":true,"error":null}
```

## glob

Search for files matching an fnmatch pattern.

**Parameters:**

| Name      | Type   | Required | Description                                  |
|-----------|--------|----------|----------------------------------------------|
| `pattern` | string | yes      | fnmatch glob pattern (supports `**`).        |
| `path`    | string | no       | Directory to search (default: cwd).          |

**Behavior:**
- Recursively walks the directory tree using `nftw()`.
- Matches relative paths against the pattern with `FNM_PATHNAME`.
- Returns up to 200 results (internal cap), displays up to 100 with a truncation
  message.
- Returns paths relative to the current working directory.

**Example output:**
```
src/main.c
src/agent.c
src/tools.c
```

## edit

Replace a unique string occurrence in a file.

**Parameters:**

| Name         | Type   | Required | Description                              |
|--------------|--------|----------|------------------------------------------|
| `path`       | string | yes      | Absolute or relative file path.          |
| `old_string` | string | yes      | Text to find. Must appear exactly once.  |
| `new_string` | string | yes      | Replacement text.                        |

**Behavior:**
- Fails if `old_string` is not found or appears more than once. When multiple
  matches exist, the error message instructs the caller to provide more
  surrounding context.
- Returns a JSON object with `success`, `path`, `start_line`, `old_line_count`,
  and `new_line_count`.

**Example output:**
```json
{"success":true,"path":"src/main.c","start_line":42,"old_line_count":3,"new_line_count":5,"error":null}
```

## shell

Execute a shell command and return its output.

**Parameters:**

| Name      | Type    | Required | Description                                |
|-----------|---------|----------|--------------------------------------------|
| `command` | string  | yes      | Shell command to execute via `/bin/sh -c`.  |
| `timeout` | integer | no       | Timeout in seconds (default: 30, max: 300). |

**Behavior:**
- Runs the command in a child process with stdout and stderr combined.
- Enforces the timeout using `select()` with non-blocking I/O. Sends `SIGKILL`
  on timeout.
- Truncates output at 512 KB.
- Returns a JSON object with `exit_code`, `stdout`, and optionally a `note`
  field if output was truncated.

**Example output:**
```json
{"exit_code":0,"stdout":"hello world\n","error":null}
```

## Tool Approval

When tools are enabled, each call goes through the approval system before
execution:

| Mode   | Behavior                                                       |
|--------|----------------------------------------------------------------|
| `auto` | All tool calls execute immediately.                            |
| `deny` | All tool calls are denied.                                     |
| `ask`  | Interactive prompt for each call (default).                    |

In `ask` mode, tools matching the `tool_allowlist` config patterns are
auto-approved. For others, the user sees:

```
Tool Call: shell
   Arguments: {"command": "ls -la"}

Approve this tool call? [Y]es [N]o [A]lways [C]ancel:
```

- **Yes**: Execute this one call.
- **No**: Deny this call (model receives a denial message).
- **Always**: Execute and auto-approve this tool for the rest of the session.
- **Cancel**: Stop the entire agent loop.
