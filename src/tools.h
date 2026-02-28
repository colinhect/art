#ifndef TOOLS_H
#define TOOLS_H

#include <cJSON.h>

typedef struct
{
    const char* name;
    const char* description;
    cJSON* parameters;
    char* (*executor)(const cJSON* args);
} tool_def_t;

/* Initialize tool parameter schemas. Call once at startup. */
void tools_init(void);
void tools_cleanup(void);

/* Build OpenAI tool schemas for tools matching fnmatch patterns.
 * patterns is NULL-terminated array. Returns cJSON array (caller owns). */
cJSON* tools_get_schemas(const char** patterns);

/* Look up a tool by name. Returns NULL if not found. */
tool_def_t* tools_find(const char* name);

/* Execute a tool by name with given args. Returns malloc'd result string. */
char* tools_execute(const char* name, const cJSON* args);

#endif
