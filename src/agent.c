#include "agent.h"
#include "api.h"
#include "buf.h"
#include "tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void agent_init(agent_t* a, http_client_t* http, const char* model, const char* system_prompt, char** tool_patterns)
{
    memset(a, 0, sizeof(*a));
    a->messages = cJSON_CreateArray();
    a->http = http;
    a->model = model;
    a->system_prompt = system_prompt ? strdup(system_prompt) : NULL;
    a->tool_patterns = tool_patterns;
}

void agent_free(agent_t* a)
{
    cJSON_Delete(a->messages);
    tool_calls_free(a->pending, a->pending_count);
    free(a->system_prompt);
    /* tool_patterns is owned by caller */
}

void agent_add_user_message(agent_t* a, const char* content)
{
    cJSON* msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", content);
    cJSON_AddItemToArray(a->messages, msg);
}

void agent_add_assistant_message(agent_t* a, const char* content, const tool_call_t* tool_calls, int tc_count)
{
    cJSON* msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "assistant");
    if (content)
    {
        cJSON_AddStringToObject(msg, "content", content);
    }

    if (tool_calls && tc_count > 0)
    {
        cJSON* tcs = cJSON_CreateArray();
        for (int i = 0; i < tc_count; i++)
        {
            cJSON* tc = cJSON_CreateObject();
            cJSON_AddStringToObject(tc, "id", tool_calls[i].id);
            cJSON_AddStringToObject(tc, "type", "function");
            cJSON* fn = cJSON_CreateObject();
            cJSON_AddStringToObject(fn, "name", tool_calls[i].name);
            cJSON_AddStringToObject(fn, "arguments", tool_calls[i].raw_args ? tool_calls[i].raw_args : "{}");
            cJSON_AddItemToObject(tc, "function", fn);
            cJSON_AddItemToArray(tcs, tc);
        }
        cJSON_AddItemToObject(msg, "tool_calls", tcs);
    }

    cJSON_AddItemToArray(a->messages, msg);
}

void agent_add_tool_result(agent_t* a, const char* tool_call_id, const char* content)
{
    cJSON* msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "tool");
    cJSON_AddStringToObject(msg, "tool_call_id", tool_call_id);
    cJSON_AddStringToObject(msg, "content", content);
    cJSON_AddItemToArray(a->messages, msg);

    /* Remove from pending */
    for (int i = 0; i < a->pending_count; i++)
    {
        if (strcmp(a->pending[i].id, tool_call_id) == 0)
        {
            /* Free this entry and shift */
            free(a->pending[i].id);
            free(a->pending[i].name);
            cJSON_Delete(a->pending[i].args);
            free(a->pending[i].raw_args);
            memmove(&a->pending[i], &a->pending[i + 1], (size_t)(a->pending_count - i - 1) * sizeof(tool_call_t));
            a->pending_count--;
            break;
        }
    }
}

void agent_pop_last_user_message(agent_t* a)
{
    int size = cJSON_GetArraySize(a->messages);
    if (size == 0)
    {
        return;
    }
    cJSON* last = cJSON_GetArrayItem(a->messages, size - 1);
    cJSON* role = cJSON_GetObjectItem(last, "role");
    if (role && cJSON_IsString(role) && strcmp(role->valuestring, "user") == 0)
    {
        cJSON_DeleteItemFromArray(a->messages, size - 1);
    }
}

void tool_calls_free(tool_call_t* tcs, int count)
{
    if (!tcs)
    {
        return;
    }
    for (int i = 0; i < count; i++)
    {
        free(tcs[i].id);
        free(tcs[i].name);
        cJSON_Delete(tcs[i].args);
        free(tcs[i].raw_args);
    }
    free(tcs);
}

/* --- SSE callback context for agent_send --- */

typedef struct
{
    buf_t id_buf;
    buf_t name_buf;
    buf_t args_buf;
} raw_tc_t;

typedef struct
{
    buf_t text;
    raw_tc_t* raw_tcs;
    int raw_tc_count;
    int raw_tc_cap;
    int input_tokens;
    int output_tokens;
    chunk_fn on_chunk;
    void* on_chunk_data;
} send_ctx_t;

static void on_sse_event(const char* json, size_t len, void* userdata)
{
    send_ctx_t* ctx = userdata;
    delta_t d;
    if (api_parse_delta(json, len, &d) < 0)
    {
        return;
    }

    /* Accumulate content and stream to caller */
    if (d.content)
    {
        buf_append_str(&ctx->text, d.content);
        if (ctx->on_chunk)
        {
            ctx->on_chunk(d.content, ctx->on_chunk_data);
        }
    }

    /* Accumulate tool call fragments */
    if (d.tool_call_index >= 0)
    {
        int idx = d.tool_call_index;
        /* Grow array if needed */
        while (ctx->raw_tc_count <= idx)
        {
            if (ctx->raw_tc_count >= ctx->raw_tc_cap)
            {
                ctx->raw_tc_cap = ctx->raw_tc_cap ? ctx->raw_tc_cap * 2 : 4;
                ctx->raw_tcs = realloc(ctx->raw_tcs, (size_t)ctx->raw_tc_cap * sizeof(raw_tc_t));
            }
            raw_tc_t* tc = &ctx->raw_tcs[ctx->raw_tc_count];
            buf_init(&tc->id_buf);
            buf_init(&tc->name_buf);
            buf_init(&tc->args_buf);
            ctx->raw_tc_count++;
        }
        raw_tc_t* tc = &ctx->raw_tcs[idx];
        if (d.tc_id)
        {
            buf_append_str(&tc->id_buf, d.tc_id);
        }
        if (d.tc_name)
        {
            buf_append_str(&tc->name_buf, d.tc_name);
        }
        if (d.tc_arguments)
        {
            buf_append_str(&tc->args_buf, d.tc_arguments);
        }
    }

    /* Usage */
    if (d.input_tokens)
    {
        ctx->input_tokens = d.input_tokens;
    }
    if (d.output_tokens)
    {
        ctx->output_tokens = d.output_tokens;
    }

    delta_free(&d);
}

int agent_send(agent_t* a, const char* prompt, chunk_fn on_chunk, void* on_chunk_data, agent_response_t* out)
{
    memset(out, 0, sizeof(*out));

    /* Add user message if non-empty */
    if (prompt && prompt[0])
    {
        agent_add_user_message(a, prompt);
        /* Clear pending tool calls on new user message */
        if (a->pending_count > 0)
        {
            tool_calls_free(a->pending, a->pending_count);
            a->pending = NULL;
            a->pending_count = 0;
        }
    }

    /* Build messages with system prompt prepended */
    cJSON* messages = cJSON_Duplicate(a->messages, 1);
    if (a->system_prompt && a->system_prompt[0])
    {
        int has_system = 0;
        if (cJSON_GetArraySize(messages) > 0)
        {
            cJSON* first = cJSON_GetArrayItem(messages, 0);
            cJSON* role = cJSON_GetObjectItem(first, "role");
            if (role && cJSON_IsString(role) && strcmp(role->valuestring, "system") == 0)
            {
                has_system = 1;
            }
        }
        if (!has_system)
        {
            cJSON* sys_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(sys_msg, "role", "system");
            cJSON_AddStringToObject(sys_msg, "content", a->system_prompt);
            cJSON_InsertItemInArray(messages, 0, sys_msg);
        }
    }

    /* Get tool schemas */
    cJSON* tools = NULL;
    if (a->tool_patterns && a->tool_patterns[0])
    {
        tools = tools_get_schemas((const char**)a->tool_patterns);
    }

    /* Build request */
    char* json_body = api_build_request(a->model, messages, tools);
    cJSON_Delete(messages);

    /* Set up SSE context */
    send_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    buf_init(&ctx.text);
    ctx.on_chunk = on_chunk;
    ctx.on_chunk_data = on_chunk_data;

    /* Make the streaming request */
    char errbuf[512] = { 0 };
    int ret = http_stream_chat(a->http, json_body, on_sse_event, &ctx, errbuf, sizeof(errbuf));
    free(json_body);
    if (tools)
    {
        cJSON_Delete(tools);
    }

    if (ret < 0)
    {
        out->error = strdup(errbuf);
        /* Pop the user message we added */
        if (prompt && prompt[0])
        {
            agent_pop_last_user_message(a);
        }
        buf_free(&ctx.text);
        for (int i = 0; i < ctx.raw_tc_count; i++)
        {
            buf_free(&ctx.raw_tcs[i].id_buf);
            buf_free(&ctx.raw_tcs[i].name_buf);
            buf_free(&ctx.raw_tcs[i].args_buf);
        }
        free(ctx.raw_tcs);
        return -1;
    }

    /* Parse accumulated tool calls */
    tool_call_t* tcs = NULL;
    int tc_count = 0;
    if (ctx.raw_tc_count > 0)
    {
        tcs = calloc((size_t)ctx.raw_tc_count, sizeof(tool_call_t));
        if (!tcs) { fprintf(stderr, "Out of memory\n"); exit(1); }
        tc_count = ctx.raw_tc_count;
        for (int i = 0; i < ctx.raw_tc_count; i++)
        {
            raw_tc_t* rtc = &ctx.raw_tcs[i];
            tcs[i].id = buf_detach(&rtc->id_buf);
            tcs[i].name = buf_detach(&rtc->name_buf);
            char* args_str = buf_detach(&rtc->args_buf);
            tcs[i].raw_args = args_str;
            tcs[i].args = cJSON_Parse(args_str);
            if (!tcs[i].args)
            {
                tcs[i].args = cJSON_CreateObject();
            }
        }
    }
    free(ctx.raw_tcs);

    /* Add assistant message to history */
    char* text = buf_detach(&ctx.text);
    if (tc_count > 0)
    {
        agent_add_assistant_message(a, (text && text[0]) ? text : NULL, tcs, tc_count);
        /* Set pending */
        tool_calls_free(a->pending, a->pending_count);
        /* Duplicate for pending (agent keeps a copy) */
        a->pending = calloc((size_t)tc_count, sizeof(tool_call_t));
        a->pending_count = tc_count;
        for (int i = 0; i < tc_count; i++)
        {
            a->pending[i].id = strdup(tcs[i].id);
            a->pending[i].name = strdup(tcs[i].name);
            a->pending[i].args = cJSON_Duplicate(tcs[i].args, 1);
            a->pending[i].raw_args = tcs[i].raw_args ? strdup(tcs[i].raw_args) : NULL;
        }
    }
    else if (text && text[0])
    {
        agent_add_assistant_message(a, text, NULL, 0);
    }

    out->text = text;
    out->tool_calls = tcs;
    out->tool_call_count = tc_count;
    out->input_tokens = ctx.input_tokens;
    out->output_tokens = ctx.output_tokens;

    return 0;
}

void agent_response_free(agent_response_t* r)
{
    free(r->text);
    tool_calls_free(r->tool_calls, r->tool_call_count);
    free(r->error);
    memset(r, 0, sizeof(*r));
}
