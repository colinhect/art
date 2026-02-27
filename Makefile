CC      ?= cc
CFLAGS   = -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS  =
LIBS     = -lcurl -lyaml -pthread

# For static builds: CC=musl-gcc LDFLAGS=-static make
# For static with mbedtls: add -lmbedtls -lmbedx509 -lmbedcrypto
# For sanitizers: SANITIZE=address make  (or thread, undefined, memory)
# For extra checks: ANALYZE=1 make
ifdef SANITIZE
CFLAGS  += -O1 -g -fsanitize=$(SANITIZE) -fno-omit-frame-pointer
LDFLAGS += -fsanitize=$(SANITIZE)
endif
ifdef ANALYZE
CFLAGS  += -fanalyzer
endif

SRCS = src/main.c src/buf.c src/config.c src/prompts.c \
       src/http.c src/sse.c src/api.c src/agent.c \
       src/runner.c src/tools.c src/session.c src/spinner.c \
       vendor/cJSON/cJSON.c

OBJS = $(SRCS:.c=.o)

art: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -Ivendor/cJSON -Isrc -c -o $@ $<

clean:
	rm -f $(OBJS) art

.PHONY: clean
