# Usage

## Synopsis

```
art [OPTIONS] [PROMPT] [@FILE ...]
```

## Options

| Flag                       | Description                                      |
|----------------------------|--------------------------------------------------|
| `-a, --agent NAME`         | Select agent from config.                        |
| `-p, --prompt-name NAME`   | Use a named prompt as the system prompt.         |
| `-s, --system-prompt TEXT`  | Provide a literal system prompt.                 |
| `--tools PATTERNS`         | Comma-separated fnmatch tool patterns.           |
| `--tool-approval MODE`     | `ask`, `auto`, or `deny`.                        |
| `--tool-output`            | Print tool execution results to stderr.          |
| `--no-session`             | Skip saving the session.                         |
| `--install`                | Create default config at `~/.artifice/`.         |
| `--add-prompt FILE`        | Copy a prompt file to `~/.artifice/prompts/`.    |
| `--new-prompt NAME`        | Create a prompt from stdin.                      |
| `--list-agents`            | List configured agents and exit.                 |
| `--list-prompts`           | List available prompts and exit.                 |
| `--get-current-agent`      | Print the active agent name and exit.            |
| `--logging`                | Enable debug logging to stderr (reserved).       |
| `-h, --help`               | Show help text.                                  |

## Basic Usage

Send a simple prompt:

```sh
art "What is the capital of France?"
```

Use a specific agent:

```sh
art -a claude "Explain this error message"
```

Provide a system prompt:

```sh
art -s "You are a Python expert" "How do I parse JSON?"
```

## File Attachments

Attach files with the `@path` syntax. The file contents are prepended to the
message:

```sh
art "Review this code" @src/main.c
```

Multiple files:

```sh
art "Compare these implementations" @v1.c @v2.c
```

The `@` prefix is only treated as a file reference if the path contains a `/` or
`\`. Plain words like `@mention` are passed through as regular arguments.

## Piped Input

art reads from stdin when it is not a terminal:

```sh
cat error.log | art "What does this error mean?"
echo "hello" | art "Translate to French"
git diff | art "Summarize these changes"
```

Stdin content is appended after any file attachments and the prompt argument.

## Tool Usage

Enable all tools:

```sh
art --tools '*' "Find all TODO comments in src/"
```

Enable specific tools:

```sh
art --tools 'read,glob' "What files are in the src directory?"
```

Auto-approve all tool calls:

```sh
art --tools '*' --tool-approval auto "Run the test suite"
```

## Prompt Management

Create a named prompt:

```sh
art --new-prompt reviewer
# Type the prompt content, then Ctrl-D
```

Or from a file:

```sh
art --add-prompt prompts/code-review.md
```

Use a named prompt:

```sh
art -p reviewer "Check this function" @lib.c
```

List available prompts:

```sh
art --list-prompts
```

Prompts are stored as `.md` files in `~/.artifice/prompts/` (global) and
`./.artifice/prompts/` (local). Local prompts override global ones with the same
name.

## Sessions

By default, art saves each conversation to
`~/.artifice/sessions/YYYY-MM-DD-HHMMSS-uuuuuu.md`. The session file contains:

- Timestamp
- Model and provider
- System prompt (if set)
- User prompt
- Model response

Disable session saving:

```sh
art --no-session "Quick question"
```

Or set `save_session: false` in the config file.

## Setup

1. Install dependencies (libcurl, libyaml).
2. Build: `make`
3. Create default config: `art --install`
4. Set your API key: `export OPENAI_API_KEY=sk-...`
5. Run: `art "Hello"`
