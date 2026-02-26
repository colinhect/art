#ifndef HTTP_H
#define HTTP_H

#include "sse.h"

#include <curl/curl.h>

typedef struct {
    char *base_url;
    char *api_key;
    CURL *curl;
} http_client_t;

int http_init(http_client_t *c, const char *base_url, const char *api_key);
void http_free(http_client_t *c);

/* POST JSON body to base_url/chat/completions with SSE streaming.
 * Calls sse_event_fn for each SSE data line. Blocks until stream ends.
 * Returns 0 on success, -1 on error. errbuf receives error details. */
int http_stream_chat(http_client_t *c, const char *json_body,
                     sse_event_fn on_event, void *userdata,
                     char *errbuf, size_t errlen);

#endif
