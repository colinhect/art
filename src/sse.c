#include "sse.h"

#include <string.h>

void sse_init(sse_parser_t* p, sse_event_fn fn, void* userdata)
{
    buf_init(&p->line_buf);
    p->on_event = fn;
    p->userdata = userdata;
}

void sse_free(sse_parser_t* p) { buf_free(&p->line_buf); }

size_t sse_feed(sse_parser_t* p, const char* data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        char c = data[i];
        if (c == '\n' || c == '\r')
        {
            if (p->line_buf.len > 6 && p->line_buf.data && strncmp(p->line_buf.data, "data: ", 6) == 0)
            {
                /* Skip [DONE] sentinel */
                if (strcmp(p->line_buf.data + 6, "[DONE]") != 0)
                {
                    p->on_event(p->line_buf.data + 6, p->line_buf.len - 6, p->userdata);
                }
            }
            buf_clear(&p->line_buf);
        }
        else
        {
            buf_append(&p->line_buf, &c, 1);
        }
    }
    return len;
}
