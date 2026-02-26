#include "tools.h"
#include "buf.h"

#include <errno.h>
#include <fnmatch.h>
#include <glob.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- Tool Executors ---- */

static char *read_file_contents(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    if (out_len)
        *out_len = n;
    fclose(f);
    return buf;
}

static const char *relative_path(const char *path) {
    /* Best-effort: if path starts with cwd, strip it */
    static char rel[4096];
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd)))
        return path;
    size_t cwdlen = strlen(cwd);
    if (strncmp(path, cwd, cwdlen) == 0 && path[cwdlen] == '/') {
        snprintf(rel, sizeof(rel), "%s", path + cwdlen + 1);
        return rel;
    }
    return path;
}

static char *resolve_path(const char *input) {
    char resolved[4096];
    /* Handle ~ expansion */
    if (input[0] == '~' && (input[1] == '/' || input[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(resolved, sizeof(resolved), "%s%s", home, input + 1);
            char *r = realpath(resolved, NULL);
            return r ? r : strdup(resolved);
        }
    }
    char *r = realpath(input, NULL);
    if (r)
        return r;
    /* If realpath fails (file doesn't exist yet), resolve relative to cwd */
    if (input[0] != '/') {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) {
            snprintf(resolved, sizeof(resolved), "%s/%s", cwd, input);
            return strdup(resolved);
        }
    }
    return strdup(input);
}

static int mkdirp(const char *path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

/* ---- read tool ---- */

static char *tool_read(const cJSON *args) {
    cJSON *jp = cJSON_GetObjectItem(args, "path");
    if (!jp || !cJSON_IsString(jp))
        return strdup("Error: 'path' parameter required");

    char *path = resolve_path(jp->valuestring);
    const char *display = relative_path(path);

    int offset = 0, limit = 0;
    cJSON *joff = cJSON_GetObjectItem(args, "offset");
    cJSON *jlim = cJSON_GetObjectItem(args, "limit");
    if (joff && cJSON_IsNumber(joff))
        offset = joff->valueint;
    if (jlim && cJSON_IsNumber(jlim))
        limit = jlim->valueint;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        buf_t b = {0};
        buf_printf(&b, "Error: File not found: %s", display);
        free(path);
        return buf_detach(&b);
    }

    /* Cap at 1MB */
    if (st.st_size > 1024 * 1024) {
        buf_t b = {0};
        buf_printf(&b, "Error: File too large (%ld bytes): %s",
                   (long)st.st_size, display);
        free(path);
        return buf_detach(&b);
    }

    size_t flen;
    char *content = read_file_contents(path, &flen);
    free(path);
    if (!content)
        return strdup("Error: Could not read file");

    if (flen == 0) {
        free(content);
        return strdup("(empty file)");
    }

    /* Split into lines and format with numbers */
    buf_t out = {0};
    int lineno = 0;
    const char *p = content;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t llen = eol ? (size_t)(eol - p) : strlen(p);
        lineno++;

        if (lineno <= offset) {
            p = eol ? eol + 1 : p + llen;
            continue;
        }
        if (limit > 0 && lineno > offset + limit)
            break;

        buf_printf(&out, "%4d | ", lineno);
        buf_append(&out, p, llen);
        buf_append_str(&out, "\n");

        p = eol ? eol + 1 : p + llen;
    }

    free(content);
    if (out.len == 0) {
        buf_free(&out);
        return strdup("(empty file)");
    }
    return buf_detach(&out);
}

/* ---- write tool ---- */

static char *tool_write(const cJSON *args) {
    cJSON *jp = cJSON_GetObjectItem(args, "path");
    cJSON *jc = cJSON_GetObjectItem(args, "content");
    if (!jp || !cJSON_IsString(jp))
        return strdup("{\"success\":false,\"error\":\"'path' parameter required\"}");
    if (!jc || !cJSON_IsString(jc))
        return strdup("{\"success\":false,\"error\":\"'content' parameter required\"}");

    char *path = resolve_path(jp->valuestring);
    const char *display = relative_path(path);
    const char *content = jc->valuestring;

    /* Check if file exists */
    struct stat st;
    int is_new = (stat(path, &st) != 0);

    /* Read old content for diff */
    char **old_lines = NULL;
    int old_count = 0;
    if (!is_new) {
        size_t flen;
        char *old = read_file_contents(path, &flen);
        if (old) {
            /* Count lines */
            const char *p = old;
            while (*p) {
                old_count++;
                const char *eol = strchr(p, '\n');
                p = eol ? eol + 1 : p + strlen(p);
            }
            free(old);
        }
    }
    (void)old_lines;

    /* mkdir -p for parent directory */
    {
        char *tmp = strdup(path);
        char *dir = dirname(tmp);
        mkdirp(dir);
        free(tmp);
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        buf_t b = {0};
        buf_printf(&b, "{\"success\":false,\"error\":\"Cannot write: %s\"}", display);
        free(path);
        return buf_detach(&b);
    }
    fputs(content, f);
    fclose(f);

    /* Count new lines */
    int new_count = 0;
    {
        const char *p = content;
        while (*p) {
            new_count++;
            const char *eol = strchr(p, '\n');
            p = eol ? eol + 1 : p + strlen(p);
        }
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "success", 1);
    cJSON_AddStringToObject(result, "path", display);
    cJSON_AddNumberToObject(result, "old_lines", old_count);
    cJSON_AddNumberToObject(result, "new_lines", new_count);
    cJSON_AddBoolToObject(result, "is_new_file", is_new);
    cJSON_AddNullToObject(result, "error");

    char *json = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    free(path);
    return json;
}

/* ---- glob tool ---- */

static char *tool_glob(const cJSON *args) {
    cJSON *jp = cJSON_GetObjectItem(args, "pattern");
    if (!jp || !cJSON_IsString(jp))
        return strdup("Error: 'pattern' parameter required");

    const char *pattern = jp->valuestring;
    cJSON *jpath = cJSON_GetObjectItem(args, "path");
    const char *base = (jpath && cJSON_IsString(jpath)) ? jpath->valuestring : ".";

    char *resolved_base = resolve_path(base);
    const char *display_base = relative_path(resolved_base);

    char full_pattern[4096];
    snprintf(full_pattern, sizeof(full_pattern), "%s/%s", resolved_base, pattern);

    glob_t gl;
    int ret = glob(full_pattern, GLOB_MARK, NULL, &gl);
    if (ret != 0) {
        buf_t b = {0};
        buf_printf(&b, "No files matching '%s' in %s", pattern, display_base);
        free(resolved_base);
        return buf_detach(&b);
    }

    buf_t out = {0};
    int max_results = 100;
    int count = 0;
    for (size_t i = 0; i < gl.gl_pathc && count < max_results; i++) {
        const char *rel = relative_path(gl.gl_pathv[i]);
        if (count > 0)
            buf_append_str(&out, "\n");
        buf_append_str(&out, rel);
        count++;
    }
    if ((int)gl.gl_pathc > max_results)
        buf_printf(&out, "\n... and %d more", (int)gl.gl_pathc - max_results);

    globfree(&gl);
    free(resolved_base);

    if (out.len == 0) {
        buf_free(&out);
        buf_t b = {0};
        buf_printf(&b, "No files matching '%s' in %s", pattern, display_base);
        return buf_detach(&b);
    }
    return buf_detach(&out);
}

/* ---- edit tool ---- */

static char *tool_edit(const cJSON *args) {
    cJSON *jp = cJSON_GetObjectItem(args, "path");
    cJSON *jo = cJSON_GetObjectItem(args, "old_string");
    cJSON *jn = cJSON_GetObjectItem(args, "new_string");
    if (!jp || !cJSON_IsString(jp) || !jo || !cJSON_IsString(jo) ||
        !jn || !cJSON_IsString(jn))
        return strdup("{\"success\":false,\"error\":\"path, old_string, and new_string required\"}");

    char *path = resolve_path(jp->valuestring);
    const char *display = relative_path(path);
    const char *old_string = jo->valuestring;
    const char *new_string = jn->valuestring;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddBoolToObject(r, "success", 0);
        buf_t eb = {0};
        buf_printf(&eb, "File not found: %s", display);
        cJSON_AddStringToObject(r, "error", eb.data);
        buf_free(&eb);
        char *json = cJSON_PrintUnformatted(r);
        cJSON_Delete(r);
        free(path);
        return json;
    }

    size_t flen;
    char *content = read_file_contents(path, &flen);
    if (!content) {
        free(path);
        return strdup("{\"success\":false,\"error\":\"Could not read file\"}");
    }

    /* Count occurrences */
    int count = 0;
    {
        const char *p = content;
        size_t oldlen = strlen(old_string);
        while ((p = strstr(p, old_string)) != NULL) {
            count++;
            p += oldlen;
        }
    }

    if (count == 0) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddBoolToObject(r, "success", 0);
        buf_t eb = {0};
        buf_printf(&eb, "String not found in %s", display);
        cJSON_AddStringToObject(r, "error", eb.data);
        buf_free(&eb);
        char *json = cJSON_PrintUnformatted(r);
        cJSON_Delete(r);
        free(content);
        free(path);
        return json;
    }

    if (count > 1) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddBoolToObject(r, "success", 0);
        buf_t eb = {0};
        buf_printf(&eb,
                   "String found %d times in %s. Provide a more specific "
                   "string with surrounding context.",
                   count, display);
        cJSON_AddStringToObject(r, "error", eb.data);
        buf_free(&eb);
        char *json = cJSON_PrintUnformatted(r);
        cJSON_Delete(r);
        free(content);
        free(path);
        return json;
    }

    /* Find the match position and line number */
    const char *match = strstr(content, old_string);
    int start_line = 1;
    for (const char *p = content; p < match; p++) {
        if (*p == '\n')
            start_line++;
    }

    /* Build new content */
    size_t oldlen = strlen(old_string);
    size_t newlen = strlen(new_string);
    size_t prefix_len = (size_t)(match - content);
    size_t suffix_len = flen - prefix_len - oldlen;
    size_t new_total = prefix_len + newlen + suffix_len;

    char *new_content = malloc(new_total + 1);
    memcpy(new_content, content, prefix_len);
    memcpy(new_content + prefix_len, new_string, newlen);
    memcpy(new_content + prefix_len + newlen, match + oldlen, suffix_len);
    new_content[new_total] = '\0';

    /* Write back */
    FILE *f = fopen(path, "w");
    if (!f) {
        free(new_content);
        free(content);
        free(path);
        return strdup("{\"success\":false,\"error\":\"Could not write file\"}");
    }
    fwrite(new_content, 1, new_total, f);
    fclose(f);

    /* Count old and new lines in the replaced segments */
    int old_line_count = 1;
    for (const char *p = old_string; *p; p++)
        if (*p == '\n')
            old_line_count++;
    int new_line_count = 1;
    for (const char *p = new_string; *p; p++)
        if (*p == '\n')
            new_line_count++;

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "success", 1);
    cJSON_AddStringToObject(result, "path", display);
    cJSON_AddNumberToObject(result, "start_line", start_line);
    cJSON_AddNumberToObject(result, "old_line_count", old_line_count);
    cJSON_AddNumberToObject(result, "new_line_count", new_line_count);
    cJSON_AddNullToObject(result, "error");

    char *json = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    free(new_content);
    free(content);
    free(path);
    return json;
}

/* ---- Tool Registry ---- */

tool_def_t TOOLS[] = {
    {.name = "read",
     .description = "Read the contents of a file.",
     .parameters = NULL,
     .executor = tool_read},
    {.name = "write",
     .description = "Write or create a file with the given content.",
     .parameters = NULL,
     .executor = tool_write},
    {.name = "glob",
     .description = "Search for files matching a glob pattern.",
     .parameters = NULL,
     .executor = tool_glob},
    {.name = "edit",
     .description = "Replace a unique string in a file with a new string. "
                    "The old_string must appear exactly once.",
     .parameters = NULL,
     .executor = tool_edit},
};

int TOOL_COUNT = sizeof(TOOLS) / sizeof(TOOLS[0]);

static cJSON *make_param(const char *type, const char *desc) {
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", type);
    if (desc)
        cJSON_AddStringToObject(p, "description", desc);
    return p;
}

void tools_init(void) {
    /* read */
    {
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "type", "object");
        cJSON *req = cJSON_CreateArray();
        cJSON_AddItemToArray(req, cJSON_CreateString("path"));
        cJSON_AddItemToObject(params, "required", req);
        cJSON *props = cJSON_CreateObject();
        cJSON_AddItemToObject(props, "path",
                              make_param("string", "Absolute or relative file path."));
        cJSON_AddItemToObject(props, "offset",
                              make_param("integer", "Line number to start reading from (0-based)."));
        cJSON_AddItemToObject(props, "limit",
                              make_param("integer", "Maximum number of lines to read."));
        cJSON_AddItemToObject(params, "properties", props);
        TOOLS[0].parameters = params;
    }
    /* write */
    {
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "type", "object");
        cJSON *req = cJSON_CreateArray();
        cJSON_AddItemToArray(req, cJSON_CreateString("path"));
        cJSON_AddItemToArray(req, cJSON_CreateString("content"));
        cJSON_AddItemToObject(params, "required", req);
        cJSON *props = cJSON_CreateObject();
        cJSON_AddItemToObject(props, "path",
                              make_param("string", "Absolute or relative file path."));
        cJSON_AddItemToObject(props, "content",
                              make_param("string", "Content to write to the file."));
        cJSON_AddItemToObject(params, "properties", props);
        TOOLS[1].parameters = params;
    }
    /* glob */
    {
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "type", "object");
        cJSON *req = cJSON_CreateArray();
        cJSON_AddItemToArray(req, cJSON_CreateString("pattern"));
        cJSON_AddItemToObject(params, "required", req);
        cJSON *props = cJSON_CreateObject();
        cJSON_AddItemToObject(props, "pattern",
                              make_param("string", "Glob pattern (supports ** for recursive)."));
        cJSON_AddItemToObject(props, "path",
                              make_param("string", "Directory to search in (default: current directory)."));
        cJSON_AddItemToObject(params, "properties", props);
        TOOLS[2].parameters = params;
    }
    /* edit */
    {
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "type", "object");
        cJSON *req = cJSON_CreateArray();
        cJSON_AddItemToArray(req, cJSON_CreateString("path"));
        cJSON_AddItemToArray(req, cJSON_CreateString("old_string"));
        cJSON_AddItemToArray(req, cJSON_CreateString("new_string"));
        cJSON_AddItemToObject(params, "required", req);
        cJSON *props = cJSON_CreateObject();
        cJSON_AddItemToObject(props, "path",
                              make_param("string", "Absolute or relative file path."));
        cJSON_AddItemToObject(props, "old_string",
                              make_param("string", "The exact text to find and replace. Must be unique."));
        cJSON_AddItemToObject(props, "new_string",
                              make_param("string", "The replacement text."));
        cJSON_AddItemToObject(params, "properties", props);
        TOOLS[3].parameters = params;
    }
}

void tools_cleanup(void) {
    for (int i = 0; i < TOOL_COUNT; i++) {
        if (TOOLS[i].parameters) {
            cJSON_Delete(TOOLS[i].parameters);
            TOOLS[i].parameters = NULL;
        }
    }
}

cJSON *tools_get_schemas(const char **patterns) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < TOOL_COUNT; i++) {
        for (const char **p = patterns; *p; p++) {
            if (fnmatch(*p, TOOLS[i].name, 0) == 0) {
                cJSON *schema = cJSON_CreateObject();
                cJSON_AddStringToObject(schema, "type", "function");
                cJSON *fn = cJSON_CreateObject();
                cJSON_AddStringToObject(fn, "name", TOOLS[i].name);
                cJSON_AddStringToObject(fn, "description",
                                        TOOLS[i].description);
                cJSON_AddItemToObject(fn, "parameters",
                                      cJSON_Duplicate(TOOLS[i].parameters, 1));
                cJSON_AddItemToObject(schema, "function", fn);
                cJSON_AddItemToArray(arr, schema);
                break;
            }
        }
    }
    return arr;
}

tool_def_t *tools_find(const char *name) {
    for (int i = 0; i < TOOL_COUNT; i++) {
        if (strcmp(TOOLS[i].name, name) == 0)
            return &TOOLS[i];
    }
    return NULL;
}

char *tools_execute(const char *name, const cJSON *args) {
    tool_def_t *t = tools_find(name);
    if (!t || !t->executor)
        return NULL;
    return t->executor(args);
}
