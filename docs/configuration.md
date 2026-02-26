# Configuration

## File Locations

art reads YAML configuration from two files, loaded in order:

1. **Global**: `~/.artifice/config.yaml`
2. **Local**: `./.artifice/config.yaml` (relative to working directory)

Local settings override global ones. If neither file exists, art still runs but
requires all settings to be provided via CLI flags.

Run `art --install` to create the global config directory and a default config
file.

## Config File Format

```yaml
# Default agent to use when --agent is not specified
agent: default

# Agent definitions
agents:
  default:
    model: gpt-4o-mini
    api_key_env: OPENAI_API_KEY

  claude:
    model: claude-sonnet-4-20250514
    api_key_env: ANTHROPIC_API_KEY
    provider: anthropic
    base_url: https://api.anthropic.com/v1
    system_prompt: You are a helpful coding assistant.
    tools:
      - read
      - write
      - shell

  local:
    model: llama3
    base_url: http://localhost:11434/v1

# Tool approval mode: "ask" (interactive), "auto", or "deny"
tool_approval: ask

# Tools auto-approved without prompting (fnmatch patterns)
tool_allowlist:
  - read
  - glob

# Whether to save sessions to ~/.artifice/sessions/
save_session: true

# Fallback system prompt (used when agent has none)
system_prompt: You are a helpful assistant.

# Prefix prepended to every user message
prompt_prefix: ""
```

## Agent Definition Fields

| Field           | Type     | Description                                              |
|-----------------|----------|----------------------------------------------------------|
| `model`         | string   | **Required.** Model identifier (e.g. `gpt-4o-mini`).    |
| `api_key`       | string   | API key (literal value). Mutually exclusive with `api_key_env`. |
| `api_key_env`   | string   | Environment variable containing the API key.             |
| `provider`      | string   | Provider name (informational, saved in session metadata).|
| `base_url`      | string   | API base URL. Defaults to `https://api.openai.com/v1`.   |
| `system_prompt` | string   | System prompt for this agent.                            |
| `tools`         | string[] | Tool names this agent is allowed to use.                 |

API key resolution: if `api_key` is set, it is used directly. Otherwise, the
value of the environment variable named by `api_key_env` is read.

## Global Settings

| Field            | Type     | Default | Description                                    |
|------------------|----------|---------|------------------------------------------------|
| `agent`          | string   | —       | Default agent name.                            |
| `tool_approval`  | string   | `ask`   | Default approval mode for tool calls.          |
| `tool_allowlist`  | string[] | —       | fnmatch patterns auto-approved in `ask` mode.  |
| `save_session`   | boolean  | `true`  | Save conversation to `~/.artifice/sessions/`.  |
| `system_prompt`  | string   | —       | Fallback system prompt if agent has none.       |
| `prompt_prefix`  | string   | —       | Prefix prepended to user messages.              |

## Precedence

CLI flags always take precedence over config file values:

```
CLI flag > local config > global config > defaults
```

Specifically:
- `--agent` overrides `agent:`
- `--system-prompt` overrides the agent's `system_prompt` and the global
  `system_prompt`
- `--prompt-name` loads a named prompt that overrides the system prompt
- `--tools` overrides the agent's `tools` list
- `--tool-approval` overrides `tool_approval:`
- `--no-session` overrides `save_session:`
