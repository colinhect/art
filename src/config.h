#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

typedef struct
{
    char* name;
    char* model;
    char* api_key;
    char* api_key_env;
    char* provider;
    char* base_url;
    char* system_prompt;
    char** tools; /* NULL-terminated */
} agent_def_t;

typedef struct
{
    char* agent;

    agent_def_t* agents;
    int agent_count;

    char* tool_approval; /* "ask", "auto", "deny" */
    char** tool_allowlist; /* NULL-terminated */

    int save_session; /* default 1 */

    char* system_prompt;
    char* prompt_prefix;
} config_t;

typedef struct
{
    char* model;
    char* api_key;
    char* provider;
    char* base_url;
    char* system_prompt;
    char** tools; /* NULL-terminated */
} resolved_agent_t;

/* Load config from ~/.artifice/config.yaml and ./.artifice/config.yaml.
 * Returns 0 on success. errbuf receives error message on failure. */
int config_load(config_t* cfg, char* errbuf, size_t errlen);
void config_free(config_t* cfg);

/* Resolve agent by name (NULL = default). Returns 0 on success. */
int resolve_agent(const config_t* cfg, const char* name, resolved_agent_t* out, char* errbuf, size_t errlen);
void resolved_agent_free(resolved_agent_t* ra);

/* Install default config to ~/.artifice/ */
int config_install(void);

/* Set the default agent in ~/.artifice/config.yaml */
int config_set_agent(const char* name);

#endif
