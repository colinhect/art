# Building

## Requirements

- C11 compiler (gcc, clang, or musl-gcc)
- libcurl development headers
- libyaml development headers
- POSIX environment (Linux, macOS, BSDs)

### Installing Dependencies

Fedora / RHEL:
```sh
sudo dnf install libcurl-devel libyaml-devel
```

Debian / Ubuntu:
```sh
sudo apt install libcurl4-openssl-dev libyaml-dev
```

Arch:
```sh
sudo pacman -S curl libyaml
```

macOS (Homebrew):
```sh
brew install curl libyaml
```

A helper script `deps.sh` is also provided in the repository root.

## Standard Build

```sh
make
```

Produces the `art` binary in the project root. Compilation uses:

```
-std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
```

## Static Build

For fully static binaries using musl:

```sh
CC=musl-gcc LDFLAGS=-static make
```

With mbedTLS instead of OpenSSL (for environments without shared OpenSSL):

```sh
CC=musl-gcc LDFLAGS=-static LIBS="-lcurl -lyaml -lmbedtls -lmbedx509 -lmbedcrypto" make
```

## Clean

```sh
make clean
```

Removes all `.o` files and the `art` binary.

## Source Layout

```
src/
├── main.c        Entry point, CLI parsing
├── agent.c/h     Conversation state, message history
├── api.c/h       OpenAI request building, SSE delta parsing
├── buf.c/h       Dynamic string buffer
├── config.c/h    YAML configuration loading
├── http.c/h      libcurl HTTP streaming client
├── prompts.c/h   Prompt file management
├── runner.c/h    Agent loop, tool approval
├── session.c/h   Session persistence
├── sse.c/h       Server-Sent Events parser
└── tools.c/h     Tool registry and executors

vendor/
└── cJSON/        Vendored JSON library (cJSON.c, cJSON.h)
```

## Compiler Database

A `compile_commands.json` is included for clangd / IDE integration. It is
generated for the default build configuration.
