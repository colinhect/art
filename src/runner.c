#include "runner.h"
#include "buf.h"
#include "tools.h"

#include <cJSON.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Tool Approver ---- */

typedef struct {
    const char *mode;       /* "ask", "auto", "deny" */
    const char **allowlist; /* fnmatch patterns, NULL-terminated */
    char **always_allowed;  /* dynamically grown set */
    int always_count;
    int always_cap;
} approver_t;

static void approver_init(approver_t *ap, const char *mode,
                          const char **allowlist) {
    ap->mode = mode ? mode : "ask";
    ap->allowlist = allowlist;
    ap->always_allowed = NULL;
    ap->always_count = 0;
    ap->always_cap = 0;
}

static void approver_free(approver_t *ap) {
    for (int i = 0; i < ap->always_count; i++)
        free(ap->always_allowed[i]);
    free(ap->always_allowed);
}

static int approver_is_allowed(approver_t *ap, const char *name) {
    if (strcmp(ap->mode, "auto") == 0)
        return 1;
    if (strcmp(ap->mode, "deny") == 0)
        return 0;

    /* Check allowlist patterns */
    if (ap->allowlist) {
        for (const char **p = ap->allowlist; *p; p++) {
            if (fnmatch(*p, name, 0) == 0)
                return 1;
        }
    }

    /* Check always-allowed set */
    for (int i = 0; i < ap->always_count; i++) {
        if (strcmp(ap->always_allowed[i], name) == 0)
            return 1;
    }

    return 0;
}

static void approver_add_always(approver_t *ap, const char *name) {
    if (ap->always_count >= ap->always_cap) {
        ap->always_cap = ap->always_cap ? ap->always_cap * 2 : 4;
        ap->always_allowed = realloc(ap->always_allowed,
                                     (size_t)ap->always_cap * sizeof(char *));
    }
    ap->always_allowed[ap->always_count++] = strdup(name);
}

/* Returns: 1 = allowed, 0 = denied. Sets *continue_session = 0 on abort. */
static int approver_check(approver_t *ap, const tool_call_t *tc,
                          int *continue_session) {
    *continue_session = 1;

    if (approver_is_allowed(ap, tc->name))
        return 1;

    /* Interactive approval */
    fprintf(stderr, "\nTool Call: %s\n", tc->name);
    if (tc->args) {
        char *args_str = cJSON_Print(tc->args);
        fprintf(stderr, "   Arguments: %s\n", args_str);
        free(args_str);
    }

    while (1) {
        fprintf(stderr,
                "\nApprove this tool call? [Y]es [N]o [A]lways [C]ancel: ");
        fflush(stderr);

        char line[64];
        if (!fgets(line, sizeof(line), stdin)) {
            fprintf(stderr, "\nOperation cancelled.\n");
            *continue_session = 0;
            return 0;
        }

        /* Strip whitespace */
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        char c = *p;
        if (c >= 'A' && c <= 'Z')
            c += 32; /* tolower */

        if (c == 'y')
            return 1;
        if (c == 'n')
            return 0;
        if (c == 'a') {
            approver_add_always(ap, tc->name);
            return 1;
        }
        if (c == 'c') {
            *continue_session = 0;
            return 0;
        }
        fprintf(stderr, "Invalid response. Please enter Y, N, A, or C.\n");
    }
}

/* ---- Format tool args for display ---- */

static void format_tool_args(const tool_call_t *tc, buf_t *out) {
    if (!tc->args)
        return;
    cJSON *item = NULL;
    int first = 1;
    cJSON_ArrayForEach(item, tc->args) {
        if (!first)
            buf_append_str(out, " ");
        first = 0;
        buf_append_str(out, item->string);
        buf_append_str(out, "=");
        if (cJSON_IsString(item)) {
            size_t len = strlen(item->valuestring);
            if (len > 40) {
                buf_append_str(out, "\"");
                buf_append(out, item->valuestring, 37);
                buf_append_str(out, "...\"");
            } else {
                buf_printf(out, "\"%s\"", item->valuestring);
            }
        } else if (cJSON_IsNumber(item)) {
            buf_printf(out, "%g", item->valuedouble);
        } else {
            buf_append_str(out, "...");
        }
    }
}

/* ---- Agent Loop ---- */

int run_agent_loop(agent_t *agent, const char *prompt,
                   chunk_fn on_chunk, void *on_chunk_data,
                   const char *tool_approval, const char **tool_allowlist,
                   int tool_output, loop_result_t *out) {
    memset(out, 0, sizeof(*out));
    buf_t final_text = {0};

    agent_response_t resp;
    int ret = agent_send(agent, prompt, on_chunk, on_chunk_data, &resp);
    if (ret < 0) {
        fprintf(stderr, "Error: %s\n", resp.error ? resp.error : "unknown");
        out->text = buf_detach(&final_text);
        agent_response_free(&resp);
        return -1;
    }

    if (resp.text)
        buf_append_str(&final_text, resp.text);
    out->input_tokens += resp.input_tokens;
    out->output_tokens += resp.output_tokens;

    approver_t ap;
    approver_init(&ap, tool_approval, tool_allowlist);

    while (resp.tool_call_count > 0) {
        fprintf(stderr, "\n");

        for (int i = 0; i < resp.tool_call_count; i++) {
            tool_call_t *tc = &resp.tool_calls[i];
            int continue_session = 1;
            int allowed = approver_check(&ap, tc, &continue_session);

            if (!continue_session) {
                fprintf(stderr, "\nOperation cancelled by user.\n");
                agent_response_free(&resp);
                approver_free(&ap);
                out->text = buf_detach(&final_text);
                return 0;
            }

            buf_t args_display = {0};
            format_tool_args(tc, &args_display);
            fprintf(stderr, "%s(%s)", tc->name,
                    args_display.data ? args_display.data : "");

            if (allowed) {
                char *result = tools_execute(tc->name, tc->args);
                if (!result)
                    result = strdup("Tool not found or no executor");
                agent_add_tool_result(agent, tc->id, result);

                if (tool_output)
                    fprintf(stderr, "\n%s", result);

                fprintf(stderr, " → +%zu chars\n", strlen(result));
                free(result);
            } else {
                buf_t denied_msg = {0};
                buf_printf(&denied_msg, "Tool call %s was denied by user",
                           tc->name);
                agent_add_tool_result(agent, tc->id, denied_msg.data);
                buf_free(&denied_msg);
                fprintf(stderr, " → denied\n");
            }
            buf_free(&args_display);
        }

        agent_response_free(&resp);

        /* Send empty prompt to get follow-up response */
        ret = agent_send(agent, "", on_chunk, on_chunk_data, &resp);
        if (ret < 0) {
            fprintf(stderr, "Error: %s\n",
                    resp.error ? resp.error : "unknown");
            agent_response_free(&resp);
            break;
        }

        if (resp.text)
            buf_append_str(&final_text, resp.text);
        out->input_tokens += resp.input_tokens;
        out->output_tokens += resp.output_tokens;
    }

    agent_response_free(&resp);
    approver_free(&ap);
    out->text = buf_detach(&final_text);
    return 0;
}

void loop_result_free(loop_result_t *r) {
    free(r->text);
    memset(r, 0, sizeof(*r));
}
