# Artifice

A minimal CLI tool for interfacing with intelligence models.

## Use

```sh
# Ask a question
art "What is the difference between a mutex and a semaphore?"

# Attach files
art "Review this" @src/main.c

# Pipe input
git diff | art "Summarize these changes"
cat error.log | art "What went wrong?"

# Use a specific agent
art -a claude "Explain this error"

# Give the model tools to act with
art --tools '*' --tool-approval auto "Find all TODO comments in src/"
```

## Tools

When you grant tools, the model can read files, write files, search with globs, make edits, and run shell commands — interactively, with your approval, or fully automatically.

```sh
# Approve each tool call interactively (default)
art --tools '*' "Refactor this function" @src/api.c

# Auto-approve everything
art --tools '*' --tool-approval auto "Run the tests and fix any failures"

# Allow only safe read-only tools
art --tools 'read,glob' "What does this codebase do?"
```

## Prompts

Save reusable system prompts by name:

```sh
art --new-prompt reviewer    # type prompt, then Ctrl-D
art -p reviewer "Check this for issues" @lib.c
```

## Sessions

Each conversation is saved to `~/.artifice/sessions/` as a Markdown file. Pass `--no-session` to skip.


## Install

```sh
# Install dependencies
sudo dnf install libcurl-devel libyaml-devel   # Fedora
sudo apt install libcurl4-openssl-dev libyaml-dev  # Debian/Ubuntu
brew install curl libyaml                          # macOS

# Build
make

# Create default config
./art --install
```

## Configure

Edit `~/.artifice/config.yaml`:

```yaml
agent: default

agents:
  default:
    model: gpt-4o-mini
    api_key_env: OPENAI_API_KEY

  claude:
    model: claude-sonnet-4-20250514
    api_key_env: ANTHROPIC_API_KEY
    provider: anthropic
    base_url: https://api.anthropic.com/v1

  local:
    model: llama3
    base_url: http://localhost:11434/v1
```

Any OpenAI-compatible API works. Set the relevant environment variable and go.

## More

Full documentation is in [`docs/`](docs/).
