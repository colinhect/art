#ifndef COPILOT_AGENT_H
#define COPILOT_AGENT_H

#include "agent.h"
#include "runner.h"

typedef struct
{
    char* text;      /* accumulated response text */
    int interrupted; /* set if SIGINT */
} copilot_result_t;

/* Run a copilot session with tool support.
 * Returns 0 on success, -1 on error. */
int run_copilot_agent(const char* model, const char* system_prompt,
    const char* prompt, char** tool_patterns,
    const char* tool_approval, const char** tool_allowlist,
    int tool_output,
    chunk_fn on_chunk, void* on_chunk_data,
    turn_fn on_turn_start, turn_fn on_turn_end,
    copilot_result_t* out);

void copilot_result_free(copilot_result_t* r);

#endif
