#ifndef BUF_H
#define BUF_H

#include <stdarg.h>
#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} buf_t;

void buf_init(buf_t *b);
void buf_append(buf_t *b, const char *s, size_t len);
void buf_append_str(buf_t *b, const char *s);
void buf_printf(buf_t *b, const char *fmt, ...);
void buf_clear(buf_t *b);
void buf_free(buf_t *b);
char *buf_detach(buf_t *b);

#endif
