#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <yaml.h>

/* Parse a YAML sequence of scalars into a NULL-terminated string array. */
static char** parse_string_list(yaml_document_t* doc, yaml_node_t* seq)
{
    if (!seq || seq->type != YAML_SEQUENCE_NODE)
    {
        return NULL;
    }
    int count = 0;
    yaml_node_item_t* item;
    for (item = seq->data.sequence.items.start; item < seq->data.sequence.items.top; item++)
    {
        count++;
    }
    char** list = calloc((size_t)count + 1, sizeof(char*));
    if (!list)
    {
        return NULL;
    }
    int i = 0;
    for (item = seq->data.sequence.items.start; item < seq->data.sequence.items.top; item++)
    {
        yaml_node_t* n = yaml_document_get_node(doc, *item);
        if (n && n->type == YAML_SCALAR_NODE)
        {
            list[i++] = strdup((const char*)n->data.scalar.value);
        }
    }
    return list;
}

/* Parse an agent definition from a YAML mapping node. */
static void parse_agent_def(yaml_document_t* doc, yaml_node_t* map, agent_def_t* a)
{
    if (!map || map->type != YAML_MAPPING_NODE)
    {
        return;
    }
    yaml_node_pair_t* pair;
    for (pair = map->data.mapping.pairs.start; pair < map->data.mapping.pairs.top; pair++)
    {
        yaml_node_t* key = yaml_document_get_node(doc, pair->key);
        yaml_node_t* val = yaml_document_get_node(doc, pair->value);
        if (!key || key->type != YAML_SCALAR_NODE)
        {
            continue;
        }
        const char* k = (const char*)key->data.scalar.value;
        if (val && val->type == YAML_SCALAR_NODE)
        {
            const char* v = (const char*)val->data.scalar.value;
            if (strcmp(k, "model") == 0)
            {
                free(a->model);
                a->model = strdup(v);
            }
            else if (strcmp(k, "api_key") == 0)
            {
                free(a->api_key);
                a->api_key = strdup(v);
            }
            else if (strcmp(k, "api_key_env") == 0)
            {
                free(a->api_key_env);
                a->api_key_env = strdup(v);
            }
            else if (strcmp(k, "provider") == 0)
            {
                free(a->provider);
                a->provider = strdup(v);
            }
            else if (strcmp(k, "base_url") == 0)
            {
                free(a->base_url);
                a->base_url = strdup(v);
            }
            else if (strcmp(k, "system_prompt") == 0)
            {
                free(a->system_prompt);
                a->system_prompt = strdup(v);
            }
        }
        else if (val && val->type == YAML_SEQUENCE_NODE)
        {
            if (strcmp(k, "tools") == 0)
            {
                free_string_list(a->tools);
                a->tools = parse_string_list(doc, val);
            }
        }
    }
}

/* Parse the top-level agents mapping. */
static void parse_agents(yaml_document_t* doc, yaml_node_t* map, config_t* cfg)
{
    if (!map || map->type != YAML_MAPPING_NODE)
    {
        return;
    }

    /* Count agents */
    int count = 0;
    yaml_node_pair_t* pair;
    for (pair = map->data.mapping.pairs.start; pair < map->data.mapping.pairs.top; pair++)
    {
        count++;
    }

    cfg->agents = calloc((size_t)count, sizeof(agent_def_t));
    cfg->agent_count = 0;

    for (pair = map->data.mapping.pairs.start; pair < map->data.mapping.pairs.top; pair++)
    {
        yaml_node_t* key = yaml_document_get_node(doc, pair->key);
        yaml_node_t* val = yaml_document_get_node(doc, pair->value);
        if (!key || key->type != YAML_SCALAR_NODE)
        {
            continue;
        }
        agent_def_t* a = &cfg->agents[cfg->agent_count++];
        memset(a, 0, sizeof(*a));
        a->name = strdup((const char*)key->data.scalar.value);
        parse_agent_def(doc, val, a);
    }
}

static int load_config_file(const char* path, config_t* cfg, char* errbuf, size_t errlen)
{
    FILE* f = fopen(path, "r");
    if (!f)
    {
        return 0; /* not found is OK */
    }

    yaml_parser_t parser;
    yaml_document_t doc;
    int ret = 0;

    if (!yaml_parser_initialize(&parser))
    {
        snprintf(errbuf, errlen, "yaml_parser_initialize failed");
        fclose(f);
        return -1;
    }
    yaml_parser_set_input_file(&parser, f);

    if (!yaml_parser_load(&parser, &doc))
    {
        snprintf(errbuf, errlen, "Error parsing %s: %s", path, parser.problem ? parser.problem : "unknown");
        yaml_parser_delete(&parser);
        fclose(f);
        return -1;
    }

    yaml_node_t* root = yaml_document_get_root_node(&doc);
    if (!root || root->type != YAML_MAPPING_NODE)
    {
        goto done;
    }

    yaml_node_pair_t* pair;
    for (pair = root->data.mapping.pairs.start; pair < root->data.mapping.pairs.top; pair++)
    {
        yaml_node_t* key = yaml_document_get_node(&doc, pair->key);
        yaml_node_t* val = yaml_document_get_node(&doc, pair->value);
        if (!key || key->type != YAML_SCALAR_NODE)
        {
            continue;
        }
        const char* k = (const char*)key->data.scalar.value;

        if (val && val->type == YAML_SCALAR_NODE)
        {
            const char* v = (const char*)val->data.scalar.value;
            if (strcmp(k, "agent") == 0)
            {
                free(cfg->agent);
                cfg->agent = strdup(v);
            }
            else if (strcmp(k, "tool_approval") == 0)
            {
                free(cfg->tool_approval);
                cfg->tool_approval = strdup(v);
            }
            else if (strcmp(k, "save_session") == 0)
            {
                cfg->save_session = (strcmp(v, "false") != 0 && strcmp(v, "0") != 0);
            }
            else if (strcmp(k, "system_prompt") == 0)
            {
                free(cfg->system_prompt);
                cfg->system_prompt = strdup(v);
            }
        }
        else if (val && val->type == YAML_MAPPING_NODE)
        {
            if (strcmp(k, "agents") == 0)
            {
                /* Free any existing agents from a previous config file */
                for (int i = 0; i < cfg->agent_count; i++)
                {
                    free(cfg->agents[i].name);
                    free(cfg->agents[i].model);
                    free(cfg->agents[i].api_key);
                    free(cfg->agents[i].api_key_env);
                    free(cfg->agents[i].provider);
                    free(cfg->agents[i].base_url);
                    free(cfg->agents[i].system_prompt);
                    free_string_list(cfg->agents[i].tools);
                }
                free(cfg->agents);
                cfg->agents = NULL;
                cfg->agent_count = 0;
                parse_agents(&doc, val, cfg);
            }
        }
        else if (val && val->type == YAML_SEQUENCE_NODE)
        {
            if (strcmp(k, "tool_allowlist") == 0)
            {
                free_string_list(cfg->tool_allowlist);
                cfg->tool_allowlist = parse_string_list(&doc, val);
            }
        }
    }

done:
    yaml_document_delete(&doc);
    yaml_parser_delete(&parser);
    fclose(f);
    return ret;
}

int config_load(config_t* cfg, char* errbuf, size_t errlen)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->save_session = 1;

    char* home_cfg = home_path("/.artifice/config.yaml");
    if (home_cfg)
    {
        int r = load_config_file(home_cfg, cfg, errbuf, errlen);
        free(home_cfg);
        if (r < 0)
        {
            config_free(cfg);
            return -1;
        }
    }

    int r = load_config_file(".artifice/config.yaml", cfg, errbuf, errlen);
    if (r < 0)
    {
        config_free(cfg);
        return -1;
    }

    /* Default tool_approval */
    if (!cfg->tool_approval)
    {
        cfg->tool_approval = strdup("ask");
    }

    return 0;
}

void config_free(config_t* cfg)
{
    free(cfg->agent);
    for (int i = 0; i < cfg->agent_count; i++)
    {
        agent_def_t* a = &cfg->agents[i];
        free(a->name);
        free(a->model);
        free(a->api_key);
        free(a->api_key_env);
        free(a->provider);
        free(a->base_url);
        free(a->system_prompt);
        free_string_list(a->tools);
    }
    free(cfg->agents);
    free(cfg->tool_approval);
    free_string_list(cfg->tool_allowlist);
    free(cfg->system_prompt);
}

int resolve_agent(const config_t* cfg, const char* name, resolved_agent_t* out, char* errbuf, size_t errlen)
{
    memset(out, 0, sizeof(*out));

    const char* agent_name = name ? name : cfg->agent;
    if (!agent_name || !cfg->agents)
    {
        snprintf(errbuf, errlen, "No agent specified. Use --agent or configure a default agent.");
        return -1;
    }

    agent_def_t* def = NULL;
    for (int i = 0; i < cfg->agent_count; i++)
    {
        if (strcmp(cfg->agents[i].name, agent_name) == 0)
        {
            def = &cfg->agents[i];
            break;
        }
    }
    if (!def)
    {
        snprintf(errbuf, errlen, "Unknown agent: '%s'", agent_name);
        return -1;
    }

    if (!def->model)
    {
        snprintf(errbuf, errlen, "Agent '%s' has no model defined", agent_name);
        return -1;
    }

    out->model = xstrdup(def->model);

    /* Resolve API key */
    out->api_key = xstrdup(def->api_key);
    if (!out->api_key && def->api_key_env)
    {
        const char* env = getenv(def->api_key_env);
        if (env)
        {
            out->api_key = strdup(env);
        }
    }

    out->provider = xstrdup(def->provider);
    out->base_url = xstrdup(def->base_url);
    out->system_prompt = xstrdup(def->system_prompt);
    if (!out->system_prompt)
    {
        out->system_prompt = xstrdup(cfg->system_prompt);
    }

    return 0;
}

void resolved_agent_free(resolved_agent_t* ra)
{
    free(ra->model);
    free(ra->api_key);
    free(ra->provider);
    free(ra->base_url);
    free(ra->system_prompt);
}

int config_set_agent(const char* name)
{
    char* cfg_path = home_path("/.artifice/config.yaml");
    if (!cfg_path)
    {
        fprintf(stderr, "Cannot determine HOME directory\n");
        return -1;
    }

    /* Read existing file */
    FILE* f = fopen(cfg_path, "r");
    if (!f)
    {
        fprintf(stderr, "Cannot open %s\n", cfg_path);
        free(cfg_path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* content = malloc((size_t)fsize + 1);
    if (!content)
    {
        fprintf(stderr, "Out of memory\n");
        fclose(f);
        free(cfg_path);
        return -1;
    }
    size_t nread = fread(content, 1, (size_t)fsize, f);
    content[nread] = '\0';
    fclose(f);

    /* Build new line */
    char new_line[512];
    snprintf(new_line, sizeof(new_line), "agent: %s", name);

    /* Scan for existing agent: line */
    char* result = NULL;
    char* line_start = content;
    char* found_start = NULL;
    char* found_end = NULL;

    while (*line_start)
    {
        char* line_end = strchr(line_start, '\n');
        size_t line_len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);

        if (strncmp(line_start, "agent:", 6) == 0)
        {
            found_start = line_start;
            found_end = line_end ? line_end : line_start + line_len;
            break;
        }

        if (!line_end)
        {
            break;
        }
        line_start = line_end + 1;
    }

    if (found_start)
    {
        /* Replace the line */
        size_t prefix_len = (size_t)(found_start - content);
        size_t suffix_len = strlen(found_end);
        size_t new_len = prefix_len + strlen(new_line) + suffix_len + 1;
        result = malloc(new_len);
        if (!result)
        {
            fprintf(stderr, "Out of memory\n");
            free(content);
            free(cfg_path);
            return -1;
        }
        memcpy(result, content, prefix_len);
        memcpy(result + prefix_len, new_line, strlen(new_line));
        memcpy(result + prefix_len + strlen(new_line), found_end, suffix_len);
        result[prefix_len + strlen(new_line) + suffix_len] = '\0';
    }
    else
    {
        /* Prepend agent line */
        size_t new_len = strlen(new_line) + 1 + strlen(content) + 1;
        result = malloc(new_len);
        if (!result)
        {
            fprintf(stderr, "Out of memory\n");
            free(content);
            free(cfg_path);
            return -1;
        }
        snprintf(result, new_len, "%s\n%s", new_line, content);
    }

    free(content);

    /* Write back */
    f = fopen(cfg_path, "w");
    if (!f)
    {
        fprintf(stderr, "Cannot write %s\n", cfg_path);
        free(result);
        free(cfg_path);
        return -1;
    }
    fputs(result, f);
    fclose(f);

    free(result);
    free(cfg_path);
    return 0;
}

int config_install(void)
{
    char* dir = home_path("/.artifice");
    char* cfg_path = home_path("/.artifice/config.yaml");
    char* prompts_dir = home_path("/.artifice/prompts");
    if (!dir || !cfg_path || !prompts_dir)
    {
        fprintf(stderr, "Cannot determine HOME directory\n");
        free(dir);
        free(cfg_path);
        free(prompts_dir);
        return -1;
    }

    /* Check if config already exists */
    struct stat st;
    if (stat(cfg_path, &st) == 0)
    {
        fprintf(stderr, "Config already exists at %s\n", cfg_path);
        free(dir);
        free(cfg_path);
        free(prompts_dir);
        return -1;
    }

    mkdir(dir, 0755);
    mkdir(prompts_dir, 0755);

    FILE* f = fopen(cfg_path, "w");
    if (!f)
    {
        fprintf(stderr, "Cannot create %s\n", cfg_path);
        free(dir);
        free(cfg_path);
        free(prompts_dir);
        return -1;
    }

    fprintf(f,
        "# Artifice configuration\n"
        "\n"
        "agent: default\n"
        "\n"
        "agents:\n"
        "  default:\n"
        "    model: gpt-4o-mini\n"
        "    api_key_env: OPENAI_API_KEY\n"
        "\n"
        "tool_approval: ask\n"
        "save_session: true\n");

    fclose(f);
    printf("Created %s\n", cfg_path);
    printf("Created %s/\n", prompts_dir);
    printf("\nEdit the config file to customize your settings.\n");

    free(dir);
    free(cfg_path);
    free(prompts_dir);
    return 0;
}
