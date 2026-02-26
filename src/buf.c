#include "buf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_INIT_CAP 256

void buf_init(buf_t* b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void buf_grow(buf_t* b, size_t need)
{
    if (b->len + need + 1 <= b->cap)
    {
        return;
    }
    size_t cap = b->cap ? b->cap : BUF_INIT_CAP;
    while (cap < b->len + need + 1)
    {
        cap *= 2;
    }
    b->data = realloc(b->data, cap);
    if (!b->data)
    {
        fprintf(stderr, "out of memory\n");
        abort();
    }
    b->cap = cap;
}

void buf_append(buf_t* b, const char* s, size_t len)
{
    if (!len)
    {
        return;
    }
    buf_grow(b, len);
    memcpy(b->data + b->len, s, len);
    b->len += len;
    b->data[b->len] = '\0';
}

void buf_append_str(buf_t* b, const char* s)
{
    if (s)
    {
        buf_append(b, s, strlen(s));
    }
}

void buf_printf(buf_t* b, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0)
    {
        return;
    }
    buf_grow(b, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

void buf_clear(buf_t* b)
{
    b->len = 0;
    if (b->data)
    {
        b->data[0] = '\0';
    }
}

void buf_free(buf_t* b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

char* buf_detach(buf_t* b)
{
    char* s = b->data;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return s;
}
