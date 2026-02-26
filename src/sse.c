#include "sse.h"

#include <string.h>

void sse_init(sse_parser_t *p, sse_event_fn fn, void *userdata) {
    p->line_len = 0;
    p->on_event = fn;
    p->userdata = userdata;
}

size_t sse_feed(sse_parser_t *p, const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n' || c == '\r') {
            p->line_buf[p->line_len] = '\0';
            /* Check for "data: " prefix */
            if (p->line_len > 6 && strncmp(p->line_buf, "data: ", 6) == 0) {
                /* Skip [DONE] sentinel */
                if (strcmp(p->line_buf + 6, "[DONE]") != 0) {
                    p->on_event(p->line_buf + 6, p->line_len - 6,
                                p->userdata);
                }
            }
            p->line_len = 0;
        } else {
            if (p->line_len < sizeof(p->line_buf) - 1)
                p->line_buf[p->line_len++] = c;
        }
    }
    return len;
}
