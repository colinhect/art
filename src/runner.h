#ifndef RUNNER_H
#define RUNNER_H

#include "agent.h"

typedef struct
{
    char* text; /* final accumulated text (all turns) */
    int input_tokens; /* total across all turns */
    int output_tokens;
} loop_result_t;

/* Called before/after each request to the model. Both are optional (may be NULL). */
typedef void (*turn_fn)(void* userdata);

/* Run the agent loop with tool calling.
 * on_turn_start: called just before each agent_send (spinner enable).
 * on_turn_end:   called just after each agent_send (spinner disable, before tool processing).
 * Returns 0 on success, -1 on error. */
int run_agent_loop(agent_t* agent, const char* prompt, chunk_fn on_chunk, void* on_chunk_data, turn_fn on_turn_start,
    turn_fn on_turn_end, const char* tool_approval, const char** tool_allowlist, int tool_output, loop_result_t* out);

void loop_result_free(loop_result_t* r);

#endif
