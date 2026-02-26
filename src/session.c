#include "session.h"
#include "buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

char* session_save(
    const char* prompt, const char* system_prompt, const char* model, const char* provider, const char* response)
{
    const char* home = getenv("HOME");
    if (!home)
    {
        return NULL;
    }

    /* mkdir -p ~/.artifice/sessions/ */
    buf_t dir = { 0 };
    buf_printf(&dir, "%s/.artifice/sessions", home);
    mkdir(dir.data, 0755);

    /* Timestamp */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);

    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d-%H%M%S", &tm);

    buf_t path = { 0 };
    buf_printf(&path, "%s/%s-%06ld.md", dir.data, ts, (long)tv.tv_usec);

    FILE* f = fopen(path.data, "w");
    if (!f)
    {
        buf_free(&dir);
        buf_free(&path);
        return NULL;
    }

    fprintf(f, "# Session: %s-%06ld\n\n", ts, (long)tv.tv_usec);
    fprintf(f, "## Model\n");
    fprintf(f, "- **Provider**: %s\n", provider ? provider : "default");
    fprintf(f, "- **Model**: %s\n\n", model);
    fprintf(f, "## System Prompt\n%s\n\n", system_prompt ? system_prompt : "(none)");
    fprintf(f, "## User Prompt\n%s\n\n", prompt);
    fprintf(f, "## Response\n%s\n", response ? response : "");

    fclose(f);
    buf_free(&dir);
    return buf_detach(&path);
}
