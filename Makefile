CC      ?= cc
CXX     ?= c++
CFLAGS   = -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
CXXFLAGS = -std=c++20 -Wall -Wextra -O2
LDFLAGS  =
LIBS     = -lcurl -lyaml -pthread -lstdc++

# For static builds: CC=musl-gcc LDFLAGS=-static make
# For static with mbedtls: add -lmbedtls -lmbedx509 -lmbedcrypto
# For sanitizers: SANITIZE=address make  (or thread, undefined, memory)
# For extra checks: ANALYZE=1 make
ifdef SANITIZE
CFLAGS  += -O1 -g -fsanitize=$(SANITIZE) -fno-omit-frame-pointer
CXXFLAGS += -O1 -g -fsanitize=$(SANITIZE) -fno-omit-frame-pointer
LDFLAGS += -fsanitize=$(SANITIZE)
endif
ifdef ANALYZE
CFLAGS  += -fanalyzer
endif

# copilot-sdk-cpp paths
COPILOT_DIR   = vendor/copilot-sdk-cpp
COPILOT_BUILD = $(COPILOT_DIR)/build
COPILOT_LIB   = $(COPILOT_BUILD)/libcopilot_sdk_cpp.a

SRCS = src/main.c src/buf.c src/config.c src/prompts.c \
       src/http.c src/sse.c src/api.c src/agent.c \
       src/runner.c src/tools.c src/session.c src/spinner.c src/util.c \
       vendor/cJSON/cJSON.c

OBJS = $(SRCS:.c=.o)
CXX_OBJS = src/copilot.o

art: $(OBJS) $(CXX_OBJS) $(COPILOT_LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(CXX_OBJS) $(COPILOT_LIB) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -Ivendor/cJSON -Isrc -c -o $@ $<

src/copilot.o: src/copilot.cpp src/copilot.h $(COPILOT_LIB)
	$(CXX) $(CXXFLAGS) -I$(COPILOT_DIR)/include \
		-I$(COPILOT_BUILD)/_deps/nlohmann_json-src/include \
		-Isrc -c -o $@ $<

$(COPILOT_LIB): $(COPILOT_DIR)/CMakeLists.txt
	cmake -S $(COPILOT_DIR) -B $(COPILOT_BUILD) \
		-DCMAKE_BUILD_TYPE=Release \
		-DCOPILOT_BUILD_TESTS=OFF \
		-DCOPILOT_BUILD_EXAMPLES=OFF \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON
	cmake --build $(COPILOT_BUILD) --target copilot_sdk_cpp

clean:
	rm -f $(OBJS) $(CXX_OBJS) art
	rm -rf $(COPILOT_BUILD)

.PHONY: clean
