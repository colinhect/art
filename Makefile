CC      ?= cc
CFLAGS   = -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS  =
LIBS     = -lcurl -lyaml

# For static builds: CC=musl-gcc LDFLAGS=-static make
# For static with mbedtls: add -lmbedtls -lmbedx509 -lmbedcrypto

SRCS = src/main.c src/buf.c src/config.c src/prompts.c \
       src/http.c src/sse.c src/api.c src/agent.c \
       src/runner.c src/tools.c src/session.c \
       vendor/cJSON/cJSON.c

OBJS = $(SRCS:.c=.o)

art: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -Ivendor/cJSON -Isrc -c -o $@ $<

clean:
	rm -f $(OBJS) art

.PHONY: clean
