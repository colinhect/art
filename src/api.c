#include "api.h"

#include <stdlib.h>
#include <string.h>

char *api_build_request(const char *model, cJSON *messages, cJSON *tools) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddBoolToObject(root, "stream", 1);

    cJSON *stream_opts = cJSON_CreateObject();
    cJSON_AddBoolToObject(stream_opts, "include_usage", 1);
    cJSON_AddItemToObject(root, "stream_options", stream_opts);

    cJSON_AddItemReferenceToObject(root, "messages", messages);

    if (tools && cJSON_GetArraySize(tools) > 0) {
        cJSON_AddItemReferenceToObject(root, "tools", tools);
        cJSON_AddStringToObject(root, "tool_choice", "auto");
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

int api_parse_delta(const char *json, size_t len, delta_t *out) {
    memset(out, 0, sizeof(*out));
    out->tool_call_index = -1;

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root)
        return -1;

    /* Usage (usually on last chunk) */
    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON *pt = cJSON_GetObjectItem(usage, "prompt_tokens");
        cJSON *ct = cJSON_GetObjectItem(usage, "completion_tokens");
        if (pt && cJSON_IsNumber(pt))
            out->input_tokens = pt->valueint;
        if (ct && cJSON_IsNumber(ct))
            out->output_tokens = ct->valueint;
    }

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || cJSON_GetArraySize(choices) == 0) {
        cJSON_Delete(root);
        return 0;
    }

    cJSON *choice0 = cJSON_GetArrayItem(choices, 0);
    cJSON *delta = cJSON_GetObjectItem(choice0, "delta");
    if (!delta) {
        cJSON_Delete(root);
        return 0;
    }

    /* Content */
    cJSON *content = cJSON_GetObjectItem(delta, "content");
    if (cJSON_IsString(content))
        out->content = strdup(content->valuestring);

    /* Tool calls */
    cJSON *tcs = cJSON_GetObjectItem(delta, "tool_calls");
    if (tcs && cJSON_GetArraySize(tcs) > 0) {
        cJSON *tc = cJSON_GetArrayItem(tcs, 0);
        cJSON *idx = cJSON_GetObjectItem(tc, "index");
        if (idx && cJSON_IsNumber(idx))
            out->tool_call_index = idx->valueint;

        cJSON *id = cJSON_GetObjectItem(tc, "id");
        if (cJSON_IsString(id))
            out->tc_id = strdup(id->valuestring);

        cJSON *fn = cJSON_GetObjectItem(tc, "function");
        if (fn) {
            cJSON *name = cJSON_GetObjectItem(fn, "name");
            cJSON *args = cJSON_GetObjectItem(fn, "arguments");
            if (cJSON_IsString(name))
                out->tc_name = strdup(name->valuestring);
            if (cJSON_IsString(args))
                out->tc_arguments = strdup(args->valuestring);
        }
    }

    cJSON_Delete(root);
    return 0;
}

void delta_free(delta_t *d) {
    free(d->content);
    free(d->tc_id);
    free(d->tc_name);
    free(d->tc_arguments);
    memset(d, 0, sizeof(*d));
    d->tool_call_index = -1;
}
