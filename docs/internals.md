# Internals

## Memory Management

art uses explicit `init`/`free` pairs for all heap-owning structures. There is
no garbage collector or reference counting. Ownership conventions:

- Functions returning `char*` transfer ownership to the caller (caller must
  `free`).
- `buf_detach()` transfers ownership of the buffer's backing memory and resets
  the `buf_t` to empty.
- Structures like `agent_t`, `config_t`, and `http_client_t` own their fields
  and free them in their `*_free()` functions.
- `main.c` uses flag variables (`cfg_loaded`, `http_initialized`, etc.) to track
  initialization state for correct cleanup via `goto` chains.

## Dynamic Buffer (`buf_t`)

The `buf_t` type is used throughout the codebase for string assembly:

```c
typedef struct {
    char* data;   // NULL-terminated string
    size_t len;   // Current length (excluding null)
    size_t cap;   // Allocated capacity
} buf_t;
```

Growth strategy: when capacity is exceeded, the buffer doubles or allocates
`len + needed`, whichever is larger. Initial allocation is 256 bytes. Buffers
can be zero-initialized (`buf_t b = {0}`) and are valid without calling
`buf_init()`.

## SSE Parser State Machine

The SSE parser (`sse_parser_t`) buffers incoming bytes one-at-a-time looking for
newline boundaries:

```
Input bytes ──► sse_feed() ──► line buffer
                                   │
                              on newline
                                   │
                           starts with "data: "?
                              │           │
                             yes          no (skip)
                              │
                        is "[DONE]"?
                           │       │
                          yes      no
                           │       │
                        (skip)   on_event(json, len)
```

This design handles arbitrary chunk boundaries from libcurl's write callback.

## Tool Call Fragment Accumulation

Tool calls arrive as incremental fragments across SSE deltas:

1. First delta for a tool call contains `id` and `name`.
2. Subsequent deltas with the same `index` append to `arguments`.
3. After the stream ends, the accumulated argument string is parsed as JSON.

The agent maintains parallel arrays indexed by tool call index to track
fragments:

```c
// In agent_send() — simplified
char* tc_ids[MAX];       // tool call IDs
char* tc_names[MAX];     // function names
buf_t tc_args[MAX];      // argument fragments
int tc_count = 0;
```

## Process Management (shell tool)

The shell tool uses `fork()`/`exec()` with pipe-based I/O:

```
Parent                          Child
  │                               │
  ├── pipe()                      │
  ├── fork() ─────────────────────┤
  │                               ├── dup2(pipe → stdout/stderr)
  │                               └── execl("/bin/sh", "-c", cmd)
  │
  ├── set O_NONBLOCK on pipe read end
  ├── select() loop with 1s timeout
  │   ├── read chunks into buf_t
  │   ├── check elapsed time vs timeout
  │   └── check output size vs 512KB cap
  │
  ├── on timeout/overflow: kill(SIGKILL)
  ├── waitpid()
  └── build JSON result
```

The non-blocking I/O with `select()` allows the parent to enforce both time and
size limits without deadlocking.

## Glob Implementation

File searching uses POSIX `nftw()` for recursive directory traversal with a
module-level context struct (not thread-safe):

```c
static struct {
    const char* pattern;    // fnmatch pattern
    const char* base;       // resolved base directory
    size_t base_len;        // for stripping prefix
    char** results;         // dynamically grown array
    int count, cap;
    int max_results;        // hard cap: 200
} glob_ctx;
```

Paths are matched as relative to the base directory using `fnmatch()` with
`FNM_PATHNAME` (so `*` does not match `/`, but `**` patterns work with
recursive matching).

## Path Resolution

All tools resolve paths through `resolve_path()`:

1. `~/...` expands to `$HOME/...`.
2. `realpath()` resolves symlinks and `.`/`..` components.
3. If `realpath()` fails (file doesn't exist yet), relative paths are prefixed
   with the current working directory.

Display paths are computed via `relative_path()`, which strips the cwd prefix
when possible.

## Configuration Parsing

YAML parsing uses libyaml's document API (not the event-based API):

1. Load the entire YAML file into a `yaml_document_t`.
2. Walk the root mapping node, matching key names.
3. For scalar values: `strdup` into config fields.
4. For the `agents` mapping: iterate sub-mappings, creating `agent_def_t` for
   each.
5. For sequence values (`tools`, `tool_allowlist`): build NULL-terminated string
   arrays.

When the local config is loaded after the global one, matching fields are
replaced (agents array is fully replaced, not merged).

## Error Handling Patterns

- Functions return `int` (0 success, -1 error) with error details in an
  `errbuf` parameter or an `error` field in the result struct.
- `main.c` uses `goto`-based cleanup chains (`cleanup_all`, `cleanup_cfg`,
  `cleanup_argv`) for orderly resource deallocation.
- Tool executors return error strings (prefixed with `"Error:"` for plain text
  or `"success":false` in JSON) that are passed back to the model as tool
  results, allowing it to self-correct.
