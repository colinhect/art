#ifndef AGENT_H
#define AGENT_H

#include "http.h"

#include <cJSON.h>

typedef void (*chunk_fn)(const char* text, void* userdata);

typedef struct
{
    char* id;
    char* name;
    cJSON* args; /* parsed JSON arguments */
    char* raw_args; /* unparsed JSON string */
} tool_call_t;

typedef struct
{
    cJSON* messages; /* cJSON array — the conversation history */
    tool_call_t* pending; /* pending tool calls from last response */
    int pending_count;

    /* Provider config */
    http_client_t* http;
    const char* model;
    char* system_prompt;
    char** tool_patterns; /* NULL-terminated, e.g. {"*", NULL} */
} agent_t;

typedef struct
{
    char* text; /* accumulated response text */
    tool_call_t* tool_calls;
    int tool_call_count;
    int input_tokens;
    int output_tokens;
    char* error; /* NULL on success */
} agent_response_t;

void agent_init(agent_t* a, http_client_t* http, const char* model, const char* system_prompt, char** tool_patterns);
void agent_free(agent_t* a);

void agent_add_user_message(agent_t* a, const char* content);
void agent_add_assistant_message(agent_t* a, const char* content, const tool_call_t* tool_calls, int tc_count);
void agent_add_tool_result(agent_t* a, const char* tool_call_id, const char* content);
void agent_pop_last_user_message(agent_t* a);

/* Send a prompt (may be empty for tool result follow-up).
 * Streams chunks via on_chunk callback. Blocks until done.
 * Returns 0 on success. On error, out->error is set. */
int agent_send(agent_t* a, const char* prompt, chunk_fn on_chunk, void* on_chunk_data, agent_response_t* out);

void agent_response_free(agent_response_t* r);

/* Free a tool_call_t array */
void tool_calls_free(tool_call_t* tcs, int count);

#endif
