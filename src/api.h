#ifndef API_H
#define API_H

#include <cJSON.h>
#include <stddef.h>

/* A single parsed SSE delta chunk. */
typedef struct
{
    char* content; /* text delta (may be NULL) */
    int tool_call_index; /* -1 if no tool call in this chunk */
    char* tc_id; /* tool call ID fragment */
    char* tc_name; /* function name fragment */
    char* tc_arguments; /* arguments JSON fragment */
    int input_tokens; /* from usage (0 if not present) */
    int output_tokens;
} delta_t;

/* Build the chat completions request body as a JSON string.
 * messages is a cJSON array. tools is a cJSON array or NULL.
 * Caller must free the returned string. */
char* api_build_request(const char* model, cJSON* messages, cJSON* tools);

/* Parse one SSE JSON chunk into a delta_t.
 * Returns 0 on success, -1 on parse error. */
int api_parse_delta(const char* json, size_t len, delta_t* out);

void delta_free(delta_t* d);

#endif
