#ifndef SSE_H
#define SSE_H

#include <stddef.h>

typedef void (*sse_event_fn)(const char *json, size_t len, void *userdata);

typedef struct {
    char line_buf[16384];
    size_t line_len;
    sse_event_fn on_event;
    void *userdata;
} sse_parser_t;

void sse_init(sse_parser_t *p, sse_event_fn fn, void *userdata);
size_t sse_feed(sse_parser_t *p, const char *data, size_t len);

#endif
