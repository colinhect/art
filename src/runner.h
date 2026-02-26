#ifndef RUNNER_H
#define RUNNER_H

#include "agent.h"

typedef struct {
    char *text;        /* final accumulated text (all turns) */
    int input_tokens;  /* total across all turns */
    int output_tokens;
} loop_result_t;

/* Run the agent loop with tool calling.
 * Returns 0 on success, -1 on error. */
int run_agent_loop(agent_t *agent, const char *prompt,
                   chunk_fn on_chunk, void *on_chunk_data,
                   const char *tool_approval, const char **tool_allowlist,
                   int tool_output, loop_result_t *out);

void loop_result_free(loop_result_t *r);

#endif
