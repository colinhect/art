#ifndef SSE_H
#define SSE_H

#include "buf.h"

#include <stddef.h>

typedef void (*sse_event_fn)(const char* json, size_t len, void* userdata);

typedef struct
{
    buf_t line_buf;
    sse_event_fn on_event;
    void* userdata;
} sse_parser_t;

void sse_init(sse_parser_t* p, sse_event_fn fn, void* userdata);
void sse_free(sse_parser_t* p);
size_t sse_feed(sse_parser_t* p, const char* data, size_t len);

#endif
