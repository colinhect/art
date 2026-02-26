#include "http.h"
#include "buf.h"
#include "sse.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

volatile sig_atomic_t g_http_interrupted = 0;

static const char* ca_paths[] = {
    "/etc/ssl/certs/ca-certificates.crt", /* Debian/Ubuntu */
    "/etc/pki/tls/certs/ca-bundle.crt", /* RHEL/Fedora */
    "/etc/ssl/cert.pem", /* macOS, Alpine */
    "/usr/share/ca-certificates/mozilla", /* Arch */
    NULL,
};

static const char* find_ca_bundle(void)
{
    const char* env = getenv("CURL_CA_BUNDLE");
    if (env)
    {
        return env;
    }
    struct stat st;
    for (const char** p = ca_paths; *p; p++)
    {
        if (stat(*p, &st) == 0)
        {
            return *p;
        }
    }
    return NULL;
}

static size_t curl_sse_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    return sse_feed((sse_parser_t*)userdata, ptr, size * nmemb);
}

static int curl_xferinfo_cb(void* p, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un)
{
    (void)p;
    (void)dt;
    (void)dn;
    (void)ut;
    (void)un;
    return (int)g_http_interrupted;
}

int http_init(http_client_t* c, const char* base_url, const char* api_key)
{
    c->base_url = strdup(base_url ? base_url : "");
    c->api_key = strdup(api_key ? api_key : "");
    c->curl = curl_easy_init();
    if (!c->curl)
    {
        free(c->base_url);
        free(c->api_key);
        return -1;
    }

    const char* ca = find_ca_bundle();
    if (ca)
    {
        curl_easy_setopt(c->curl, CURLOPT_CAINFO, ca);
    }

    return 0;
}

void http_free(http_client_t* c)
{
    if (c->curl)
    {
        curl_easy_cleanup(c->curl);
    }
    c->curl = NULL;
    free(c->base_url);
    free(c->api_key);
    c->base_url = NULL;
    c->api_key = NULL;
}

int http_stream_chat(
    http_client_t* c, const char* json_body, sse_event_fn on_event, void* userdata, char* errbuf, size_t errlen)
{
    sse_parser_t parser;
    sse_init(&parser, on_event, userdata);

    buf_t url = { 0 };
    buf_printf(&url, "%s/chat/completions", c->base_url);

    curl_easy_setopt(c->curl, CURLOPT_URL, url.data);
    curl_easy_setopt(c->curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(c->curl, CURLOPT_POST, 1L);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    buf_t auth = { 0 };
    if (c->api_key && c->api_key[0])
    {
        buf_printf(&auth, "Authorization: Bearer %s", c->api_key);
        headers = curl_slist_append(headers, auth.data);
    }

    curl_easy_setopt(c->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c->curl, CURLOPT_WRITEFUNCTION, curl_sse_write_cb);
    curl_easy_setopt(c->curl, CURLOPT_WRITEDATA, &parser);
    curl_easy_setopt(c->curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c->curl, CURLOPT_XFERINFOFUNCTION, curl_xferinfo_cb);

    CURLcode res = curl_easy_perform(c->curl);

    long http_code = 0;
    curl_easy_getinfo(c->curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    buf_free(&url);
    buf_free(&auth);

    /* Reset for reuse */
    curl_easy_setopt(c->curl, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(c->curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(c->curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(c->curl, CURLOPT_XFERINFOFUNCTION, NULL);

    int ret = 0;
    if (res == CURLE_ABORTED_BY_CALLBACK)
    {
        errbuf[0] = '\0';
        ret = -1;
    }
    else if (res != CURLE_OK)
    {
        snprintf(errbuf, errlen, "curl error: %s", curl_easy_strerror(res));
        ret = -1;
    }
    else if (http_code >= 400)
    {
        /* Include any response body the SSE parser accumulated */
        if (parser.line_buf.data && parser.line_buf.len > 0)
        {
            snprintf(errbuf, errlen, "HTTP %ld: %s", http_code, parser.line_buf.data);
        }
        else
        {
            snprintf(errbuf, errlen, "HTTP %ld", http_code);
        }
        ret = -1;
    }

    sse_free(&parser);
    return ret;
}
