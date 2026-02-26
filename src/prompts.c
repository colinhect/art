#include "prompts.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char* home_prompts_dir(void)
{
    const char* home = getenv("HOME");
    if (!home)
    {
        return NULL;
    }
    size_t len = strlen(home) + strlen("/.artifice/prompts") + 1;
    char* p = malloc(len);
    snprintf(p, len, "%s/.artifice/prompts", home);
    return p;
}

static int dir_exists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void scan_dir(const char* dirpath, char*** names, int* count, int* cap)
{
    DIR* d = opendir(dirpath);
    if (!d)
    {
        return;
    }

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL)
    {
        const char* name = ent->d_name;
        size_t nlen = strlen(name);
        if (nlen < 4 || strcmp(name + nlen - 3, ".md") != 0)
        {
            continue;
        }

        /* Strip .md extension */
        char* base = strndup(name, nlen - 3);

        /* Check for duplicates (local overrides home) */
        int found = 0;
        for (int i = 0; i < *count; i++)
        {
            if (strcmp((*names)[i], base) == 0)
            {
                found = 1;
                break;
            }
        }
        if (found)
        {
            free(base);
            continue;
        }

        if (*count >= *cap)
        {
            *cap = *cap ? *cap * 2 : 16;
            *names = realloc(*names, (size_t)*cap * sizeof(char*));
        }
        (*names)[(*count)++] = base;
    }
    closedir(d);
}

char** list_prompts(void)
{
    char** names = NULL;
    int count = 0, cap = 0;

    /* Local first (takes priority) */
    if (dir_exists(".artifice/prompts"))
    {
        scan_dir(".artifice/prompts", &names, &count, &cap);
    }

    /* Then home */
    char* hdir = home_prompts_dir();
    if (hdir)
    {
        if (dir_exists(hdir))
        {
            scan_dir(hdir, &names, &count, &cap);
        }
        free(hdir);
    }

    /* NULL-terminate */
    if (!names)
    {
        return NULL;
    }
    names = realloc(names, (size_t)(count + 1) * sizeof(char*));
    names[count] = NULL;
    return names;
}

char* load_prompt(const char* name)
{
    /* Try local first, then home */
    const char* dirs[2];
    char local_dir[] = ".artifice/prompts";
    dirs[0] = local_dir;
    char* hdir = home_prompts_dir();
    dirs[1] = hdir;

    char path[4096];
    for (int i = 0; i < 2; i++)
    {
        if (!dirs[i])
        {
            continue;
        }
        if (!dir_exists(dirs[i]))
        {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s.md", dirs[i], name);
        FILE* f = fopen(path, "r");
        if (!f)
        {
            continue;
        }
        struct stat fst;
        if (fstat(fileno(f), &fst) < 0 || !S_ISREG(fst.st_mode))
        {
            fclose(f);
            continue;
        }
        size_t sz = (size_t)fst.st_size;
        char* buf = malloc(sz + 1);
        size_t n = fread(buf, 1, sz, f);
        buf[n] = '\0';
        fclose(f);
        free(hdir);
        return buf;
    }

    free(hdir);
    return NULL;
}
