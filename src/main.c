#include "agent.h"
#include "buf.h"
#include "config.h"
#include "http.h"
#include "prompts.h"
#include "runner.h"
#include "session.h"
#include "tools.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <curl/curl.h>

static void print_chunk(const char *text, void *userdata) {
    (void)userdata;
    fputs(text, stdout);
    fflush(stdout);
}

/* Read all of stdin into a malloc'd string. */
static char *read_stdin(void) {
    buf_t b = {0};
    char tmp[4096];
    size_t n;
    while ((n = fread(tmp, 1, sizeof(tmp), stdin)) > 0)
        buf_append(&b, tmp, n);
    return buf_detach(&b);
}

/* Resolve @file arguments from argv. */
static void resolve_at_files(int argc, char **argv,
                             char ***out_remaining, int *out_remaining_count,
                             char ***out_files, int *out_file_count) {
    *out_remaining = calloc((size_t)argc, sizeof(char *));
    *out_files = calloc((size_t)argc, sizeof(char *));
    *out_remaining_count = 0;
    *out_file_count = 0;

    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == '@' && argv[i][1] != '\0') {
            const char *filename = argv[i] + 1;
            if (strchr(filename, '/') || strchr(filename, '\\')) {
                struct stat st;
                if (stat(filename, &st) == 0 && S_ISREG(st.st_mode)) {
                    (*out_files)[(*out_file_count)++] = strdup(filename);
                } else {
                    fprintf(stderr, "Error: File not found: %s\n", filename);
                    exit(1);
                }
            } else {
                (*out_remaining)[(*out_remaining_count)++] = argv[i];
            }
        } else {
            (*out_remaining)[(*out_remaining_count)++] = argv[i];
        }
    }
}

static char *build_user_message(const char *prompt_arg, int is_tty,
                                char **files, int file_count) {
    buf_t msg = {0};

    /* Read file attachments */
    if (file_count > 0) {
        for (int i = 0; i < file_count; i++) {
            FILE *f = fopen(files[i], "r");
            if (!f) {
                fprintf(stderr, "Error reading %s\n", files[i]);
                exit(1);
            }
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *content = malloc((size_t)sz + 1);
            size_t n = fread(content, 1, (size_t)sz, f);
            content[n] = '\0';
            fclose(f);

            if (msg.len > 0)
                buf_append_str(&msg, "\n\n");
            buf_printf(&msg, "--- %s ---\n%s", files[i], content);
            free(content);
        }
        if (msg.len > 0)
            buf_append_str(&msg, "\n\n---\n\n");
    }

    /* Prompt from argument */
    if (prompt_arg && prompt_arg[0])
        buf_append_str(&msg, prompt_arg);

    /* Stdin content */
    if (!is_tty) {
        char *stdin_content = read_stdin();
        if (stdin_content && stdin_content[0]) {
            if (msg.len > 0)
                buf_append_str(&msg, "\n\n");
            buf_append_str(&msg, stdin_content);
        }
        free(stdin_content);
    }

    return buf_detach(&msg);
}

static char **parse_tool_patterns(const char *tools_arg) {
    if (!tools_arg)
        return NULL;

    /* Count commas */
    int count = 1;
    for (const char *p = tools_arg; *p; p++)
        if (*p == ',')
            count++;

    char **patterns = calloc((size_t)count + 1, sizeof(char *));
    char *tmp = strdup(tools_arg);
    char *tok = strtok(tmp, ",");
    int i = 0;
    while (tok) {
        /* Trim whitespace */
        while (*tok == ' ')
            tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && *end == ' ')
            *end-- = '\0';
        patterns[i++] = strdup(tok);
        tok = strtok(NULL, ",");
    }
    free(tmp);
    return patterns;
}

static void free_string_array(char **arr) {
    if (!arr)
        return;
    for (int i = 0; arr[i]; i++)
        free(arr[i]);
    free(arr);
}

enum {
    OPT_TOOLS = 256,
    OPT_TOOL_APPROVAL,
    OPT_TOOL_OUTPUT,
    OPT_NO_SESSION,
    OPT_INSTALL,
    OPT_ADD_PROMPT,
    OPT_NEW_PROMPT,
    OPT_LIST_AGENTS,
    OPT_LIST_PROMPTS,
    OPT_GET_CURRENT_AGENT,
    OPT_LOGGING,
};

static struct option long_options[] = {
    {"agent", required_argument, 0, 'a'},
    {"prompt-name", required_argument, 0, 'p'},
    {"system-prompt", required_argument, 0, 's'},
    {"tools", required_argument, 0, OPT_TOOLS},
    {"tool-approval", required_argument, 0, OPT_TOOL_APPROVAL},
    {"tool-output", no_argument, 0, OPT_TOOL_OUTPUT},
    {"no-session", no_argument, 0, OPT_NO_SESSION},
    {"install", no_argument, 0, OPT_INSTALL},
    {"add-prompt", required_argument, 0, OPT_ADD_PROMPT},
    {"new-prompt", required_argument, 0, OPT_NEW_PROMPT},
    {"list-agents", no_argument, 0, OPT_LIST_AGENTS},
    {"list-prompts", no_argument, 0, OPT_LIST_PROMPTS},
    {"get-current-agent", no_argument, 0, OPT_GET_CURRENT_AGENT},
    {"logging", no_argument, 0, OPT_LOGGING},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0},
};

static void usage(void) {
    fprintf(stderr,
        "Usage: art [OPTIONS] [PROMPT]\n"
        "\n"
        "Simple LLM prompt tool with tool support.\n"
        "\n"
        "Options:\n"
        "  -a, --agent NAME          Agent name from config\n"
        "  -p, --prompt-name NAME    Named prompt for system prompt\n"
        "  -s, --system-prompt TEXT   Literal system prompt\n"
        "      --tools PATTERNS      Comma-separated tool patterns (e.g. '*')\n"
        "      --tool-approval MODE  ask, auto, or deny\n"
        "      --tool-output         Show tool execution output\n"
        "      --no-session          Don't save session\n"
        "      --install             Install default config\n"
        "      --add-prompt FILE     Add a prompt file\n"
        "      --new-prompt NAME     Create prompt from stdin\n"
        "      --list-agents         List agents and exit\n"
        "      --list-prompts        List prompts and exit\n"
        "      --get-current-agent   Print current agent and exit\n"
        "      --logging             Enable debug logging to stderr\n"
        "  -h, --help                Show this help\n");
}

int main(int argc, char **argv) {
    /* Resolve @file arguments */
    char **remaining_argv = NULL;
    int remaining_argc = 0;
    char **attached_files = NULL;
    int attached_count = 0;
    resolve_at_files(argc - 1, argv + 1, &remaining_argv, &remaining_argc,
                     &attached_files, &attached_count);

    /* Rebuild argv for getopt (prepend program name) */
    char **new_argv = calloc((size_t)remaining_argc + 2, sizeof(char *));
    new_argv[0] = argv[0];
    for (int i = 0; i < remaining_argc; i++)
        new_argv[i + 1] = remaining_argv[i];
    int new_argc = remaining_argc + 1;

    /* Parse options */
    char *opt_agent = NULL;
    char *opt_prompt_name = NULL;
    char *opt_system_prompt = NULL;
    char *opt_tools = NULL;
    char *opt_tool_approval = NULL;
    int opt_tool_output = 0;
    int opt_no_session = 0;
    int opt_install = 0;
    char *opt_add_prompt = NULL;
    char *opt_new_prompt = NULL;
    int opt_list_agents = 0;
    int opt_list_prompts = 0;
    int opt_get_current_agent = 0;
    int opt_logging = 0;

    optind = 1;
    int c;
    while ((c = getopt_long(new_argc, new_argv, "a:p:s:h", long_options,
                            NULL)) != -1) {
        switch (c) {
        case 'a': opt_agent = optarg; break;
        case 'p': opt_prompt_name = optarg; break;
        case 's': opt_system_prompt = optarg; break;
        case OPT_TOOLS: opt_tools = optarg; break;
        case OPT_TOOL_APPROVAL: opt_tool_approval = optarg; break;
        case OPT_TOOL_OUTPUT: opt_tool_output = 1; break;
        case OPT_NO_SESSION: opt_no_session = 1; break;
        case OPT_INSTALL: opt_install = 1; break;
        case OPT_ADD_PROMPT: opt_add_prompt = optarg; break;
        case OPT_NEW_PROMPT: opt_new_prompt = optarg; break;
        case OPT_LIST_AGENTS: opt_list_agents = 1; break;
        case OPT_LIST_PROMPTS: opt_list_prompts = 1; break;
        case OPT_GET_CURRENT_AGENT: opt_get_current_agent = 1; break;
        case OPT_LOGGING: opt_logging = 1; break;
        case 'h':
            usage();
            free(remaining_argv);
            free(new_argv);
            return 0;
        default:
            usage();
            free(remaining_argv);
            free(new_argv);
            return 1;
        }
    }

    /* Positional prompt argument */
    const char *prompt_arg = NULL;
    if (optind < new_argc)
        prompt_arg = new_argv[optind];

    (void)opt_logging; /* TODO: structured logging */

    /* Handle --install */
    if (opt_install) {
        int ret = config_install();
        free(remaining_argv);
        free(new_argv);
        for (int i = 0; i < attached_count; i++)
            free(attached_files[i]);
        free(attached_files);
        return ret < 0 ? 1 : 0;
    }

    /* Handle --add-prompt */
    if (opt_add_prompt) {
        const char *home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "Cannot determine HOME\n");
            return 1;
        }
        char dir[4096], dest[4096];
        snprintf(dir, sizeof(dir), "%s/.artifice/prompts", home);
        mkdir(dir, 0755);

        /* Get basename */
        const char *base = strrchr(opt_add_prompt, '/');
        base = base ? base + 1 : opt_add_prompt;

        snprintf(dest, sizeof(dest), "%s/%s", dir, base);
        struct stat st;
        if (stat(dest, &st) == 0) {
            fprintf(stderr, "Error: Prompt already exists: %s\n", dest);
            return 1;
        }

        /* Copy file */
        FILE *src = fopen(opt_add_prompt, "r");
        if (!src) {
            fprintf(stderr, "Error: File not found: %s\n", opt_add_prompt);
            return 1;
        }
        FILE *dst = fopen(dest, "w");
        if (!dst) {
            fclose(src);
            fprintf(stderr, "Error: Cannot create %s\n", dest);
            return 1;
        }
        char tmp[4096];
        size_t n;
        while ((n = fread(tmp, 1, sizeof(tmp), src)) > 0)
            fwrite(tmp, 1, n, dst);
        fclose(src);
        fclose(dst);
        printf("Added prompt: %s\n", dest);
        return 0;
    }

    /* Handle --new-prompt */
    if (opt_new_prompt) {
        const char *home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "Cannot determine HOME\n");
            return 1;
        }
        char dir[4096], dest[4096];
        snprintf(dir, sizeof(dir), "%s/.artifice/prompts", home);
        mkdir(dir, 0755);

        const char *name = opt_new_prompt;
        size_t nlen = strlen(name);
        if (nlen < 3 || strcmp(name + nlen - 3, ".md") != 0)
            snprintf(dest, sizeof(dest), "%s/%s.md", dir, name);
        else
            snprintf(dest, sizeof(dest), "%s/%s", dir, name);

        struct stat st;
        if (stat(dest, &st) == 0) {
            fprintf(stderr, "Error: Prompt already exists: %s\n", dest);
            return 1;
        }

        if (isatty(STDIN_FILENO))
            fprintf(stderr,
                    "Enter prompt content for '%s'. Press Ctrl-D to save.\n",
                    opt_new_prompt);
        char *content = read_stdin();
        FILE *f = fopen(dest, "w");
        if (!f) {
            fprintf(stderr, "Error: Cannot create %s\n", dest);
            free(content);
            return 1;
        }
        if (content)
            fputs(content, f);
        fclose(f);
        free(content);
        printf("Created prompt: %s\n", dest);
        return 0;
    }

    /* Load config */
    config_t cfg;
    char errbuf[512];
    if (config_load(&cfg, errbuf, (int)sizeof(errbuf)) < 0) {
        fprintf(stderr, "Configuration error: %s\n", errbuf);
        return 1;
    }

    /* Handle --list-agents */
    if (opt_list_agents) {
        for (int i = 0; i < cfg.agent_count; i++)
            printf("%s\n", cfg.agents[i].name);
        config_free(&cfg);
        free(remaining_argv);
        free(new_argv);
        return 0;
    }

    /* Handle --list-prompts */
    if (opt_list_prompts) {
        char **prompts = list_prompts();
        if (prompts) {
            for (int i = 0; prompts[i]; i++) {
                printf("%s\n", prompts[i]);
                free(prompts[i]);
            }
            free(prompts);
        }
        config_free(&cfg);
        free(remaining_argv);
        free(new_argv);
        return 0;
    }

    /* Handle --get-current-agent */
    if (opt_get_current_agent) {
        const char *name = opt_agent ? opt_agent : cfg.agent;
        if (!name) {
            fprintf(stderr, "Error: No agent configured\n");
            config_free(&cfg);
            return 1;
        }
        printf("%s\n", name);
        config_free(&cfg);
        free(remaining_argv);
        free(new_argv);
        return 0;
    }

    /* Build user message */
    int is_tty = isatty(STDIN_FILENO);
    char *prompt = build_user_message(prompt_arg, is_tty,
                                      attached_files, attached_count);

    if (!prompt || !prompt[0]) {
        free(prompt);
        config_free(&cfg);
        free(remaining_argv);
        free(new_argv);
        for (int i = 0; i < attached_count; i++)
            free(attached_files[i]);
        free(attached_files);
        return 0;
    }

    /* Resolve agent config */
    resolved_agent_t ra;
    if (resolve_agent(&cfg, opt_agent, &ra, errbuf, (int)sizeof(errbuf)) < 0) {
        fprintf(stderr, "Error: %s\n", errbuf);
        free(prompt);
        config_free(&cfg);
        free(remaining_argv);
        free(new_argv);
        return 1;
    }

    /* Resolve system prompt */
    char *system_prompt = NULL;
    if (opt_system_prompt) {
        system_prompt = strdup(opt_system_prompt);
    } else if (opt_prompt_name) {
        system_prompt = load_prompt(opt_prompt_name);
        if (!system_prompt) {
            fprintf(stderr, "Error: Unknown prompt '%s'\n", opt_prompt_name);
            free(prompt);
            resolved_agent_free(&ra);
            config_free(&cfg);
            return 1;
        }
    } else if (ra.system_prompt) {
        system_prompt = strdup(ra.system_prompt);
    }

    /* Parse tool patterns */
    char **tool_patterns = parse_tool_patterns(opt_tools);

    /* Determine tool_approval */
    const char *tool_approval = opt_tool_approval ? opt_tool_approval
                                                  : cfg.tool_approval;

    /* Initialize curl globally */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    /* Initialize tools */
    tools_init();

    /* Set up HTTP client */
    const char *base_url = ra.base_url;
    if (!base_url || !base_url[0])
        base_url = "https://api.openai.com/v1";

    http_client_t http;
    if (http_init(&http, base_url, ra.api_key ? ra.api_key : "") < 0) {
        fprintf(stderr, "Error: Failed to initialize HTTP client\n");
        free(prompt);
        free(system_prompt);
        free_string_array(tool_patterns);
        resolved_agent_free(&ra);
        config_free(&cfg);
        return 1;
    }

    /* Initialize agent */
    agent_t agent;
    agent_init(&agent, &http, ra.model, system_prompt, tool_patterns);

    /* Run the agent loop */
    loop_result_t result;
    int ret = run_agent_loop(&agent, prompt, print_chunk, NULL,
                             tool_approval,
                             (const char **)(cfg.tool_allowlist),
                             opt_tool_output, &result);

    /* Trailing newline */
    if (result.text && result.text[0] &&
        result.text[strlen(result.text) - 1] != '\n')
        printf("\n");

    /* Save session */
    if (cfg.save_session && !opt_no_session && ret == 0) {
        char *path = session_save(prompt, system_prompt, ra.model,
                                  ra.provider, result.text);
        if (path)
            free(path);
    }

    /* Cleanup */
    loop_result_free(&result);
    agent_free(&agent);
    http_free(&http);
    tools_cleanup();
    curl_global_cleanup();
    free(prompt);
    /* system_prompt is owned by agent (don't double-free) */
    free_string_array(tool_patterns);
    resolved_agent_free(&ra);
    config_free(&cfg);
    free(remaining_argv);
    free(new_argv);
    for (int i = 0; i < attached_count; i++)
        free(attached_files[i]);
    free(attached_files);

    return ret < 0 ? 1 : 0;
}
