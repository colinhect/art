#include "copilot_agent.h"
#include "buf.h"
#include "copilot.h"
#include "tools.h"

#include <cJSON.h>
#include <fnmatch.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shared interrupted flag (from main.c via http.h) */
extern volatile sig_atomic_t g_http_interrupted;

/* ---- Tool Approver (duplicated from runner.c — keep in sync) ---- */

typedef struct
{
    const char* mode;
    const char** allowlist;
    char** always_allowed;
    int always_count;
    int always_cap;
} cp_approver_t;

static void cp_approver_init(cp_approver_t* ap, const char* mode, const char** allowlist)
{
    ap->mode = mode ? mode : "ask";
    ap->allowlist = allowlist;
    ap->always_allowed = NULL;
    ap->always_count = 0;
    ap->always_cap = 0;
}

static void cp_approver_free(cp_approver_t* ap)
{
    for (int i = 0; i < ap->always_count; i++)
    {
        free(ap->always_allowed[i]);
    }
    free(ap->always_allowed);
}

static int cp_approver_is_allowed(cp_approver_t* ap, const char* name)
{
    if (strcmp(ap->mode, "auto") == 0)
    {
        return 1;
    }
    if (strcmp(ap->mode, "deny") == 0)
    {
        return 0;
    }

    if (ap->allowlist)
    {
        for (const char** p = ap->allowlist; *p; p++)
        {
            if (fnmatch(*p, name, 0) == 0)
            {
                return 1;
            }
        }
    }

    for (int i = 0; i < ap->always_count; i++)
    {
        if (strcmp(ap->always_allowed[i], name) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static void cp_approver_add_always(cp_approver_t* ap, const char* name)
{
    if (ap->always_count >= ap->always_cap)
    {
        ap->always_cap = ap->always_cap ? ap->always_cap * 2 : 4;
        char** tmp = realloc(ap->always_allowed, (size_t)ap->always_cap * sizeof(char*));
        if (!tmp)
        {
            fprintf(stderr, "Out of memory\n");
            exit(1);
        }
        ap->always_allowed = tmp;
    }
    ap->always_allowed[ap->always_count++] = strdup(name);
}

/* Returns: 1 = allowed, 0 = denied. */
static int cp_approver_check(cp_approver_t* ap, const char* name, const char* args_json)
{
    if (cp_approver_is_allowed(ap, name))
    {
        return 1;
    }

    /* Interactive approval */
    fprintf(stderr, "\nTool Call: %s\n", name);
    if (args_json)
    {
        fprintf(stderr, "   Arguments: %s\n", args_json);
    }

    while (1)
    {
        fprintf(stderr, "\nApprove this tool call? [Y]es [N]o [A]lways: ");
        fflush(stderr);

        char line[64];
        if (!fgets(line, sizeof(line), stdin))
        {
            return 0;
        }

        char* p = line;
        while (*p == ' ' || *p == '\t')
        {
            p++;
        }
        char c = *p;
        if (c >= 'A' && c <= 'Z')
        {
            c += 32;
        }

        if (c == 'y')
        {
            return 1;
        }
        if (c == 'n')
        {
            return 0;
        }
        if (c == 'a')
        {
            cp_approver_add_always(ap, name);
            return 1;
        }
        fprintf(stderr, "Invalid response. Please enter Y, N, or A.\n");
    }
}

/* ---- Callback context ---- */

typedef struct
{
    /* Streaming callbacks */
    chunk_fn on_chunk;
    void* on_chunk_data;
    turn_fn on_turn_start;
    turn_fn on_turn_end;

    /* Tool approval */
    cp_approver_t approver;
    int tool_output;

    /* Accumulated result */
    buf_t text;
    char* error;
} copilot_ctx_t;

/* ---- Tool callback ---- */

static char* copilot_tool_handler(const char* name, const char* args_json, void* userdata)
{
    copilot_ctx_t* ctx = userdata;

    /* End spinner for tool processing */
    if (ctx->on_turn_end)
    {
        ctx->on_turn_end(ctx->on_chunk_data);
    }

    fprintf(stderr, "\n");

    int allowed = cp_approver_check(&ctx->approver, name, args_json);

    if (!allowed)
    {
        buf_t denied = { 0 };
        buf_printf(&denied, "Tool call %s was denied by user", name);
        fprintf(stderr, "%s() → denied\n", name);

        /* Restart spinner for next model turn */
        if (ctx->on_turn_start)
        {
            ctx->on_turn_start(ctx->on_chunk_data);
        }
        return buf_detach(&denied);
    }

    /* Parse args JSON into cJSON for tools_execute */
    cJSON* args = cJSON_Parse(args_json);

    /* Display tool call */
    fprintf(stderr, "%s(", name);
    if (args)
    {
        cJSON* item = NULL;
        int first = 1;
        cJSON_ArrayForEach(item, args)
        {
            if (!first)
            {
                fprintf(stderr, " ");
            }
            first = 0;
            if (cJSON_IsString(item))
            {
                size_t len = strlen(item->valuestring);
                if (len > 40)
                {
                    fprintf(stderr, "%s=\"%.37s...\"", item->string, item->valuestring);
                }
                else
                {
                    fprintf(stderr, "%s=\"%s\"", item->string, item->valuestring);
                }
            }
            else if (cJSON_IsNumber(item))
            {
                fprintf(stderr, "%s=%g", item->string, item->valuedouble);
            }
            else
            {
                fprintf(stderr, "%s=...", item->string);
            }
        }
    }
    fprintf(stderr, ")");

    char* result = tools_execute(name, args);
    cJSON_Delete(args);

    if (!result)
    {
        result = strdup("Tool not found or no executor");
    }

    if (ctx->tool_output)
    {
        fprintf(stderr, "\n%s", result);
    }

    fprintf(stderr, " → +%zu chars\n", strlen(result));

    /* Restart spinner for next model turn */
    if (ctx->on_turn_start)
    {
        ctx->on_turn_start(ctx->on_chunk_data);
    }

    return result;
}

/* ---- Event callback ---- */

static void copilot_event_handler(copilot_event_type type, const char* data_json, void* userdata)
{
    copilot_ctx_t* ctx = userdata;

    switch (type)
    {
    case COPILOT_EVENT_ASSISTANT_MESSAGE_DELTA:
    {
        /* Extract delta from JSON */
        cJSON* root = cJSON_Parse(data_json);
        if (root)
        {
            cJSON* delta = cJSON_GetObjectItem(root, "delta");
            if (delta && cJSON_IsString(delta) && delta->valuestring[0])
            {
                if (ctx->on_chunk)
                {
                    ctx->on_chunk(delta->valuestring, ctx->on_chunk_data);
                }
            }
            cJSON_Delete(root);
        }
        break;
    }
    case COPILOT_EVENT_ASSISTANT_MESSAGE:
    {
        /* Accumulate final text */
        cJSON* root = cJSON_Parse(data_json);
        if (root)
        {
            cJSON* content = cJSON_GetObjectItem(root, "content");
            if (content && cJSON_IsString(content))
            {
                buf_append_str(&ctx->text, content->valuestring);
            }
            cJSON_Delete(root);
        }
        break;
    }
    case COPILOT_EVENT_SESSION_ERROR:
    {
        cJSON* root = cJSON_Parse(data_json);
        if (root)
        {
            cJSON* msg = cJSON_GetObjectItem(root, "message");
            if (msg && cJSON_IsString(msg))
            {
                free(ctx->error);
                ctx->error = strdup(msg->valuestring);
            }
            cJSON_Delete(root);
        }
        break;
    }
    default:
        break;
    }
}

/* ---- Public API ---- */

int run_copilot_agent(const char* model, const char* system_prompt,
    const char* prompt, char** tool_patterns,
    const char* tool_approval, const char** tool_allowlist,
    int tool_output,
    chunk_fn on_chunk, void* on_chunk_data,
    turn_fn on_turn_start, turn_fn on_turn_end,
    copilot_result_t* out)
{
    memset(out, 0, sizeof(*out));
    int ret = -1;

    /* Initialize context */
    copilot_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.on_chunk = on_chunk;
    ctx.on_chunk_data = on_chunk_data;
    ctx.on_turn_start = on_turn_start;
    ctx.on_turn_end = on_turn_end;
    ctx.tool_output = tool_output;
    cp_approver_init(&ctx.approver, tool_approval, tool_allowlist);

    /* Create and start client */
    copilot_client_t* client = copilot_client_create();
    if (!client)
    {
        fprintf(stderr, "Error: Failed to create copilot client: %s\n",
            copilot_last_error() ? copilot_last_error() : "unknown");
        goto cleanup_ctx;
    }

    if (copilot_client_start(client) < 0)
    {
        fprintf(stderr, "Error: Failed to start copilot client: %s\n",
            copilot_last_error() ? copilot_last_error() : "unknown");
        goto cleanup_client;
    }

    /* Create session */
    copilot_session_t* session = copilot_session_create(client, model, system_prompt);
    if (!session)
    {
        fprintf(stderr, "Error: Failed to create copilot session: %s\n",
            copilot_last_error() ? copilot_last_error() : "unknown");
        goto cleanup_stop;
    }

    /* Register tools matching patterns */
    if (tool_patterns)
    {
        tools_init();

        cJSON* schemas = tools_get_schemas((const char**)tool_patterns);
        int count = cJSON_GetArraySize(schemas);
        for (int i = 0; i < count; i++)
        {
            cJSON* schema = cJSON_GetArrayItem(schemas, i);
            cJSON* fn = cJSON_GetObjectItem(schema, "function");
            if (!fn)
            {
                continue;
            }

            const char* name = cJSON_GetObjectItem(fn, "name")->valuestring;
            const char* desc = cJSON_GetObjectItem(fn, "description")->valuestring;
            cJSON* params = cJSON_GetObjectItem(fn, "parameters");
            char* params_json = params ? cJSON_PrintUnformatted(params) : NULL;

            copilot_session_register_tool(session, name, desc, params_json,
                copilot_tool_handler, &ctx);

            free(params_json);
        }
        cJSON_Delete(schemas);
    }

    /* Start spinner for the first model turn */
    if (on_turn_start)
    {
        on_turn_start(on_chunk_data);
    }

    /* Send message and wait for completion */
    if (copilot_session_send_and_wait(session, prompt, copilot_event_handler, &ctx, 300) < 0)
    {
        if (!g_http_interrupted)
        {
            fprintf(stderr, "Error: Copilot send failed: %s\n",
                copilot_last_error() ? copilot_last_error() : "unknown");
        }
        if (on_turn_end)
        {
            on_turn_end(on_chunk_data);
        }
        goto cleanup_session;
    }

    /* End spinner */
    if (on_turn_end)
    {
        on_turn_end(on_chunk_data);
    }

    /* Check for errors */
    if (ctx.error)
    {
        fprintf(stderr, "Error: %s\n", ctx.error);
        goto cleanup_session;
    }

    ret = 0;

cleanup_session:
    copilot_session_destroy(session);
cleanup_stop:
    copilot_client_stop(client);
cleanup_client:
    copilot_client_destroy(client);
cleanup_ctx:
    out->text = buf_detach(&ctx.text);
    out->interrupted = g_http_interrupted;
    free(ctx.error);
    cp_approver_free(&ctx.approver);
    return ret;
}

void copilot_result_free(copilot_result_t* r)
{
    free(r->text);
    memset(r, 0, sizeof(*r));
}
