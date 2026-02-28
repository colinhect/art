#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char* xstrdup(const char* s) { return s ? strdup(s) : NULL; }

char* home_path(const char* suffix)
{
    const char* home = getenv("HOME");
    if (!home)
    {
        return NULL;
    }
    size_t len = strlen(home) + strlen(suffix) + 1;
    char* p = malloc(len);
    if (!p)
    {
        return NULL;
    }
    snprintf(p, len, "%s%s", home, suffix);
    return p;
}

char* read_file_contents(const char* path, size_t* out_len)
{
    FILE* f = fopen(path, "r");
    if (!f)
    {
        return NULL;
    }
    struct stat st;
    if (fstat(fileno(f), &st) < 0 || !S_ISREG(st.st_mode))
    {
        fclose(f);
        return NULL;
    }
    size_t sz = (size_t)st.st_size;
    char* buf = malloc(sz + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, sz, f);
    buf[n] = '\0';
    if (out_len)
    {
        *out_len = n;
    }
    fclose(f);
    return buf;
}

void free_string_list(char** list)
{
    if (!list)
    {
        return;
    }
    for (int i = 0; list[i]; i++)
    {
        free(list[i]);
    }
    free(list);
}
